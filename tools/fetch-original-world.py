#!/usr/bin/env python3
"""Fetch exact World source blobs from a previously verified fixed Git tree."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import time
import urllib.parse
import urllib.request

COMMIT = '09df8928eeb15328bca499d225e48ea95af6d119'
ROOT = Path(__file__).resolve().parents[1]


def blob_sha(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tree', type=Path)
    parser.add_argument('--workers', type=int, default=12)
    args = parser.parse_args()
    tree = json.loads(args.tree.read_text())
    if tree.get('truncated') or tree['sha'] != '2ab95905f3053ebe2721a2fa03f83d6fd849c8c9':
        raise ValueError('Expected complete pinned Minecraft.World root tree')
    target = ROOT / 'port/upstream/Minecraft.World'
    entries = [e for e in tree['tree'] if e['type'] == 'blob' and
               e['path'].endswith(('.cpp', '.h', '.inl', '.txt'))]

    def fetch(entry):
        name = entry['path']
        if Path(name).name != name:
            return {'file': name, 'status': 'invalid-path'}
        path = target / name
        if path.exists():
            data = path.read_bytes()
            if blob_sha(data) != entry['sha']:
                return {'file': name, 'status': 'existing-hash-mismatch'}
            return {'file': name, 'status': 'verified-existing', 'sha': entry['sha']}
        error = ''
        for attempt in range(3):
            try:
                url = f'https://raw.githubusercontent.com/gstaaij/minecraft-console/{COMMIT}/Minecraft.World/' + urllib.parse.quote(name)
                request = urllib.request.Request(url, headers={'User-Agent': 'Minecraft-original-source-port'})
                with urllib.request.urlopen(request, timeout=20) as response:
                    data = response.read()
                if len(data) != entry['size'] or blob_sha(data) != entry['sha']:
                    raise ValueError('Git blob hash mismatch')
                with path.open('xb') as output:
                    output.write(data)
                return {'file': name, 'status': 'downloaded-verified', 'sha': entry['sha']}
            except Exception as exc:
                error = str(exc)
                time.sleep(attempt + 1)
        return {'file': name, 'status': 'failed', 'error': error}

    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        jobs = [pool.submit(fetch, entry) for entry in entries]
        for future in as_completed(jobs):
            result = future.result()
            results.append(result)
            if len(results) % 50 == 0 or result['status'] in ('failed', 'existing-hash-mismatch'):
                successes = sum(r['status'] in ('verified-existing', 'downloaded-verified') for r in results)
                print(f'{len(results)}/{len(entries)} checked, {successes} verified; last={result}', flush=True)
    report = {'repository': 'https://github.com/gstaaij/minecraft-console.git',
              'commit': COMMIT, 'world_tree': tree['sha'], 'expected': len(entries),
              'verified': sum(r['status'] in ('verified-existing', 'downloaded-verified') for r in results),
              'files': sorted(results, key=lambda r: r['file'])}
    output = ROOT / 'port/original-world-fetch.json'
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Final: {report["verified"]}/{report["expected"]} verified; report: {output}')
    return 0 if report['verified'] == report['expected'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
