#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a MinGW-w64 x86_64 sysroot from signed MSYS2 mingw64 packages.

  mingw64-sysroot.py WORKDIR SYSROOT [package ...]

Downloads the mingw64 repo database and its signature, checks the signature
with gpg against the MSYS2 keyring, resolves the runtime dependency closure
of the requested packages, downloads each package, checks its sha256 against
the signed database, unpacks it under SYSROOT/mingw64, and rewrites the
pkg-config prefixes to point there.

Needs: python3, gpg, tar with zstd support.
"""
import hashlib, json, os, re, subprocess, sys, urllib.request

REPO = 'https://repo.msys2.org/mingw/mingw64/'
KEYRING = 'https://raw.githubusercontent.com/msys2/MSYS2-keyring/master/'
SKIP = {'mingw-w64-x86_64-python', 'mingw-w64-x86_64-gcc'}
DEFAULT = ['sdl3', 'cairo', 'pango', 'ttf-dejavu']


def fetch(url, dest):
    if not os.path.exists(dest):
        print('fetch', url, flush=True)
        urllib.request.urlretrieve(url, dest + '.part')
        os.replace(dest + '.part', dest)


def main():
    work, sysroot = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    wanted = sys.argv[3:] or DEFAULT
    os.makedirs(os.path.join(work, 'pkgs'), exist_ok=True)
    db, sig = (os.path.join(work, n) for n in ('mingw64.db', 'mingw64.db.sig'))
    fetch(REPO + 'mingw64.db', db)
    fetch(REPO + 'mingw64.db.sig', sig)
    kr = {}
    for n in ('msys2.gpg', 'msys2-trusted', 'msys2-revoked'):
        kr[n] = os.path.join(work, n)
        fetch(KEYRING + n, kr[n])

    # Same trust model as pacman-key --populate msys2: a throwaway local key
    # lsigns the MSYS2 master keys, which then certify the packager keys.
    gnupg = os.path.join(work, 'gnupg')
    os.makedirs(gnupg, mode=0o700, exist_ok=True)
    g = ['gpg', '--homedir', gnupg, '--batch', '--quiet']
    run = lambda *a, **k: subprocess.run(g + list(a), check=True, capture_output=True, **k)
    run('--import', kr['msys2.gpg'])
    if subprocess.run(g + ['--list-secret-keys'], capture_output=True).stdout.strip() == b'':
        run('--passphrase', '', '--quick-gen-key', 'lunduke-city-sysroot-verify', 'ed25519', 'sign', 'never')
    masters = [l.split(':')[0] for l in open(kr['msys2-trusted']).read().split() if l]
    for fpr in masters:
        run('--yes', '--quick-lsign-key', fpr)
    run('--import-ownertrust', kr['msys2-trusted'])
    run('--check-trustdb')
    revoked = set(open(kr['msys2-revoked']).read().split())
    st = subprocess.run(g + ['--status-fd', '1', '--verify', sig, db], capture_output=True, text=True)
    good = re.search(r'^\[GNUPG:\] VALIDSIG (?:\S+ ){9}(\S+)', st.stdout, re.M)
    trusted = re.search(r'^\[GNUPG:\] TRUST_(FULLY|ULTIMATE)', st.stdout, re.M)
    if st.returncode != 0 or not good or not trusted or good.group(1) in revoked:
        sys.exit('mingw64.db signature is not valid and trusted:\n' + st.stdout + st.stderr)
    print('mingw64.db signed by', good.group(1), '(trusted via MSYS2 master keys)')

    pk, prov = {}, {}
    dbdir = os.path.join(work, 'db')
    os.makedirs(dbdir, exist_ok=True)
    subprocess.run(['tar', '-C', dbdir, '-xf', db], check=True)  # zstd-compressed
    for entry in sorted(os.listdir(dbdir)):
        sec, cur = {}, None
        for line in open(os.path.join(dbdir, entry, 'desc'), encoding='utf-8').read().splitlines():
            h = re.match(r'^%(\w+)%$', line)
            if h:
                cur = h.group(1); sec[cur] = []
            elif line and cur:
                sec[cur].append(line)
        n = sec['NAME'][0]; pk[n] = sec
        for p in sec.get('PROVIDES', []):
            prov.setdefault(re.split('[<>=]', p)[0], n)

    def resolve(dep):
        b = re.split('[<>=]', dep)[0]
        return b if b in pk else prov.get(b)

    seen, stack = [], ['mingw-w64-x86_64-' + w for w in wanted]
    while stack:
        n = resolve(stack.pop())
        if n is None or n in seen or n in SKIP:
            continue
        seen.append(n)
        stack += pk[n].get('DEPENDS', [])

    manifest = []
    for n in sorted(seen):
        s = pk[n]; fn = s['FILENAME'][0]; path = os.path.join(work, 'pkgs', fn)
        fetch(REPO + fn, path)
        digest = hashlib.sha256(open(path, 'rb').read()).hexdigest()
        if digest != s['SHA256SUM'][0]:
            sys.exit('sha256 mismatch: ' + fn)
        os.makedirs(sysroot, exist_ok=True)
        subprocess.run(['tar', '-C', sysroot, '--exclude=.*', '-xf', path], check=True)
        manifest.append({'file': fn, 'version': s['VERSION'][0], 'sha256': digest})
        print('ok', fn)

    prefix = os.path.join(sysroot, 'mingw64')
    for d in ('lib/pkgconfig', 'share/pkgconfig'):
        pcdir = os.path.join(prefix, d)
        for f in os.listdir(pcdir) if os.path.isdir(pcdir) else []:
            p = os.path.join(pcdir, f)
            txt = open(p).read()
            txt = re.sub(r'^prefix=.*$', 'prefix=' + prefix, txt, flags=re.M)
            open(p, 'w').write(txt)
    json.dump(manifest, open(os.path.join(sysroot, 'packages.json'), 'w'), indent=1)
    print(len(manifest), 'packages verified and unpacked into', prefix)


if __name__ == '__main__':
    main()
