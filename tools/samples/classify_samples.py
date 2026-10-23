#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Classification des samples de CyDAW par CLAP (zero-shot).

Appele par le DAW : `python classify_samples.py requete.json`. La requete
porte {"files": [...], "categories": [...], "tags": [...]}. La sortie est une
ligne JSON par fichier : {"path", "category", "tags", "confidence"},
precedee d'une ligne {"classifier": "clap" | "unavailable", "message"}.

Modeles essayes dans l'ordre : laion_clap (pip install laion-clap), puis
transformers (laion/clap-htsat-unfused). Sans l'un des deux, la reponse est
"unavailable" et le DAW garde ses categories deduites des noms.
"""
import json
import os
import sys

CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".clap-cache")
# The models' downloads stay beside the classifier (next to CyDAW), never in the
# user's profile on C: (Hugging Face's and torch's default caches).
for _variable in ("HF_HOME", "HUGGINGFACE_HUB_CACHE", "TRANSFORMERS_CACHE", "TORCH_HOME"):
    os.environ.setdefault(_variable, CACHE_DIR)


def emit(value):
    sys.stdout.write(json.dumps(value, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def prompts_for(categories, tags):
    drum = {"kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "drums"}
    category_prompts = []
    for category in categories:
        if category in drum:
            category_prompts.append("the sound of a %s drum sample" % category)
        elif category == "fx":
            category_prompts.append("a sound effect, riser, impact or sweep")
        elif category == "other":
            category_prompts.append("a miscellaneous sound")
        else:
            category_prompts.append("the sound of a %s" % category)
    tag_prompts = ["a %s sound" % tag for tag in tags]
    return category_prompts, tag_prompts


def softmax(values, temperature=0.05):
    import math
    top = max(values)
    weights = [math.exp((v - top) / temperature) for v in values]
    total = sum(weights) or 1.0
    return [w / total for w in weights]


def cosine(a, b):
    import math
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a)) or 1.0
    nb = math.sqrt(sum(y * y for y in b)) or 1.0
    return dot / (na * nb)


class LaionClap:
    name = "laion_clap"

    def __init__(self):
        import laion_clap  # noqa: F401
        self.model = laion_clap.CLAP_Module(enable_fusion=False)
        self.model.load_ckpt()

    def audio(self, files):
        return self.model.get_audio_embedding_from_filelist(x=files, use_tensor=False)

    def text(self, prompts):
        return self.model.get_text_embedding(prompts, use_tensor=False)


class TransformersClap:
    name = "transformers"

    def __init__(self):
        import torch
        from transformers import ClapModel, ClapProcessor
        self.torch = torch
        self.model = ClapModel.from_pretrained("laion/clap-htsat-unfused")
        self.processor = ClapProcessor.from_pretrained("laion/clap-htsat-unfused")

    def audio(self, files):
        import librosa
        embeddings = []
        for path in files:
            waveform, _ = librosa.load(path, sr=48000, mono=True, duration=30.0)
            inputs = self.processor(audios=waveform, sampling_rate=48000, return_tensors="pt")
            with self.torch.no_grad():
                embeddings.append(self.model.get_audio_features(**inputs)[0].tolist())
        return embeddings

    def text(self, prompts):
        inputs = self.processor(text=prompts, return_tensors="pt", padding=True)
        with self.torch.no_grad():
            return self.model.get_text_features(**inputs).tolist()


def load_backend():
    errors = []
    for backend in (LaionClap, TransformersClap):
        try:
            return backend(), None
        except Exception as error:  # noqa: BLE001 - reported to the DAW
            errors.append("%s: %s" % (backend.name, error))
    return None, " ; ".join(errors)


def main():
    if len(sys.argv) < 2 or not os.path.isfile(sys.argv[1]):
        emit({"classifier": "unavailable", "message": "requete manquante"})
        return
    with open(sys.argv[1], encoding="utf-8") as handle:
        request = json.load(handle)
    files = [f for f in request.get("files", []) if os.path.isfile(f)]
    categories = request.get("categories") or ["other"]
    tags = request.get("tags") or []
    backend, error = load_backend()
    if backend is None:
        emit({"classifier": "unavailable",
              "message": "CLAP non installe (pip install laion-clap) : " + (error or "")})
        return
    emit({"classifier": "clap", "message": "CLAP via " + backend.name})
    category_prompts, tag_prompts = prompts_for(categories, tags)
    try:
        category_embeddings = backend.text(category_prompts)
        tag_embeddings = backend.text(tag_prompts) if tag_prompts else []
        audio_embeddings = backend.audio(files)
    except Exception as error:  # noqa: BLE001
        emit({"classifier": "unavailable", "message": "CLAP a echoue : %s" % error})
        return
    for path, embedding in zip(files, audio_embeddings):
        scores = [cosine(embedding, text) for text in category_embeddings]
        probabilities = softmax(scores)
        best = max(range(len(categories)), key=lambda i: probabilities[i])
        chosen = []
        if tag_embeddings:
            tag_scores = [(cosine(embedding, text), tag) for text, tag in zip(tag_embeddings, tags)]
            tag_scores.sort(reverse=True)
            top = tag_scores[0][0] if tag_scores else 0.0
            chosen = [tag for score, tag in tag_scores[:4] if score >= top - 0.03]
        emit({"path": path, "category": categories[best],
              "tags": ",".join(chosen), "confidence": round(probabilities[best], 3)})


if __name__ == "__main__":
    main()
