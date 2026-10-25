"""Publish a verified CyDAW V2 Alpha built by GitHub Actions. Never publishes stable releases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def check_tag(tag):
    match = re.search(r'project\(CyDAW VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text('utf-8'))
    if not match or not re.fullmatch(r'v2-' + re.escape(match.group(1)) + r'-alpha\.[1-9][0-9]*', tag):
        raise ValueError('Tag must be v2-<current CMake version>-alpha.<positive number>')
    return match.group(1)


def verified_assets(folder):
    folder = Path(folder)
    assets = sorted(p for p in folder.iterdir() if p.is_file())
    checksums = {}
    for line in (folder / 'SHA256SUMS.txt').read_text('utf-8').splitlines():
        digest, name = line.split('  ', 1)
        if Path(name).name != name or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('Invalid checksum entry')
        path = folder / name
        with path.open('rb') as handle:
            if hashlib.file_digest(handle, 'sha256').hexdigest() != digest:
                raise ValueError('Checksum mismatch: ' + name)
        checksums[name] = digest
    if not checksums or not all(name.endswith('.zip') for name in checksums):
        raise ValueError('Expected packaged ZIP assets')
    expected = set(checksums) | {name + '.sha256' for name in checksums} | {'SHA256SUMS.txt'}
    if {p.name for p in assets} != expected:
        raise ValueError('Unexpected or missing release assets')
    for name, digest in checksums.items():
        if (folder / (name + '.sha256')).read_text('utf-8').strip() != digest + '  ' + name:
            raise ValueError('Checksum sidecar mismatch: ' + name)
    return assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-tag')
    parser.add_argument('--tag')
    parser.add_argument('--assets', type=Path)
    args = parser.parse_args()
    if args.check_tag:
        check_tag(args.check_tag)
        print('Valid CyDAW V2 Alpha tag: ' + args.check_tag)
        return
    if not args.tag or not args.assets:
        parser.error('--tag and --assets are required')
    version = check_tag(args.tag)
    assets = verified_assets(args.assets)
    token = os.environ['GITHUB_TOKEN']
    repository = os.environ['GITHUB_REPOSITORY']
    sha = os.environ['GITHUB_SHA']
    if not re.fullmatch('[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository) or not re.fullmatch('[0-9a-f]{40}', sha):
        raise ValueError('Invalid Actions repository/commit identity')
    api = 'https://api.github.com/repos/' + repository

    def request(method, url, payload=None, stream=None, size=None):
        headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
                   'User-Agent': 'CyDAW-V2-Alpha', 'X-GitHub-Api-Version': '2022-11-28'}
        data = json.dumps(payload).encode('utf-8') if payload is not None else None
        if data is not None:
            headers['Content-Type'] = 'application/json'
        if stream is not None:
            data = stream
            headers.update({'Content-Type': 'application/octet-stream', 'Content-Length': str(size)})
        with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers, method=method), timeout=600) as response:
            return json.load(response)

    def optional(url):
        try:
            return request('GET', url)
        except urllib.error.HTTPError as exc:
            if exc.code != 404:
                raise
            return None

    ref = optional(api + '/git/ref/tags/' + args.tag)
    if ref:
        obj = ref['object']
        while obj['type'] == 'tag':
            obj = request('GET', obj['url'])['object']
        if obj['sha'] != sha:
            raise ValueError('Release tag already points to another commit; use a new Alpha number')
    title = 'CyDAW V2 ' + version + ' — Alpha ' + args.tag.rsplit('.', 1)[1]
    body = ('## Alpha preview — CyDAW V2\n\n'
            'Built and tested on GitHub Actions for Windows x64. This is the Dear ImGui **CyDAW V2** application.\n\n'
            'Download the ZIP below, extract it completely, and launch **CyDAW 2.exe**. Keep the DLLs and folders beside it.\n\n'
            '- Windows Audio / WASAPI; this package does not enable ASIO or VST2.\n'
            '- Studio, Mix DJ, Foundry, Cinema, Processings and Plugin workshop.\n'
            '- Native instruments/effects, isolated VST3/CLAP/LV2/JSFX hosting and video codecs.\n'
            '- Factory sample banks are downloaded separately in Options > Sound banks. Local example projects and unfinished sound models may be absent.\n'
            '- Building exported plugins requires the C++ tools and CyDAW sources.\n\n'
            '**Experimental Alpha / préversion expérimentale.** Windows x64 only. SHA-256 checksums are supplied.\n\n'
            'Source: [' + sha[:7] + '](https://github.com/' + repository + '/commit/' + sha + ').\n')
    release = optional(api + '/releases/tags/' + args.tag)
    if release is None:
        release = request('POST', api + '/releases', {'tag_name': args.tag, 'target_commitish': sha,
                          'name': title, 'body': body, 'draft': True, 'prerelease': True, 'make_latest': 'false'})
    present = {a['name']: a for a in release.get('assets', [])}
    expected = {}
    for path in assets:
        with path.open('rb') as handle:
            digest = hashlib.file_digest(handle, 'sha256').hexdigest()
        expected[path.name] = digest
        asset = present.get(path.name)
        if asset is None:
            print('Uploading ' + path.name, flush=True)
            url = release['upload_url'].split('{')[0] + '?' + urllib.parse.urlencode({'name': path.name})
            with path.open('rb') as handle:
                asset = request('POST', url, stream=handle, size=path.stat().st_size)
        if asset['state'] != 'uploaded' or asset['size'] != path.stat().st_size or asset.get('digest') != 'sha256:' + digest:
            raise ValueError('Remote asset integrity mismatch: ' + path.name)
    release = request('GET', api + '/releases/' + str(release['id']))
    if {a['name'] for a in release['assets']} != set(expected):
        raise ValueError('Unexpected release asset list')
    release = request('PATCH', api + '/releases/' + str(release['id']),
                      {'name': title, 'body': body, 'draft': False, 'prerelease': True, 'make_latest': 'false'})
    if release['draft'] or not release['prerelease']:
        raise ValueError('Alpha release state is invalid')
    print('Published: ' + release['html_url'])
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as handle:
            handle.write('## ' + title + '\n\n[Download the Alpha](' + release['html_url'] + ')\n')


if __name__ == '__main__':
    main()
