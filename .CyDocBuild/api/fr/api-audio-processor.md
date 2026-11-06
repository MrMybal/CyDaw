## Includes et opérations publiques {#signatures}
Ces déclarations décrivent l’interface virtuelle `cydaw::engine::AudioProcessor`. Dériver un processeur et implémenter `prepare`, `reset` et `process` ; les méthodes de latence et de mesures possèdent une implémentation par défaut.

```cpp
#include <cydaw/engine/AudioProcessor.h>
void prepare(const cydaw::engine::ProcessSpec& spec);
void reset() noexcept;
void process(cydaw::engine::AudioBlockView input,
             cydaw::engine::AudioBlockView output,
             const cydaw::engine::ProcessContext& context) noexcept;
std::uint32_t latencySamples() const noexcept;
std::uint32_t maximumLatencySamples() const noexcept;
std::size_t readings(float* into, std::size_t count) const noexcept;
```

## Paramètres et résultats {#parameters}
- `ProcessSpec` fournit `sampleRate` en hertz, `maximumBlockSize` en trames et `channelCount`. Allouer les ressources réutilisables pendant `prepare`.
- `input` et `output` sont des `AudioBlockView` sans propriété de leur mémoire ; leur stockage doit rester valide pendant le callback. `process` ne renvoie rien et écrit l’audio de sortie.
- `context.transport` est l’état du transport pour le bloc, déclaré dans `<cydaw/engine/Transport.h>`.
- `latencySamples` renvoie un nombre d’échantillons. Après préparation, il ne doit jamais dépasser `maximumLatencySamples`.
- `readings(into, count)` écrit au plus `count` mesures d’affichage et renvoie leur nombre. La lecture simultanée d’un état partagé demande une synchronisation adaptée, sans bloquer le thread audio.

## Exemple de préparation {#example}
Pour une instance de processeur existante nommée `processor`, préparer le traitement avant de fournir les tampons :

```cpp
cydaw::engine::ProcessSpec spec;
spec.sampleRate = 48000.0;
spec.maximumBlockSize = 512;
spec.channelCount = 2;
processor.prepare(spec);
processor.reset();
```

`AudioBlockView` est déclaré dans `<cydaw/engine/AudioBuffer.h>`. Pour les processeurs dont les ports d’entrée restent distincts, `MultiBusAudioProcessor::processAudioBuses(std::span<const AudioInputBusView>, AudioBlockView, const ProcessContext&) noexcept` reçoit des bus dont `portId` désigne le port créé dans le graphe. Voir le [routage](doc:mixing#sidechain).
