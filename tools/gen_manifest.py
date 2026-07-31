#!/usr/bin/env python3
"""
Builds a signed OTA manifest.json from a built firmware image
(docs/PLATFORM.md §2.3): computes sha256/size of the image, signs the
canonical-bytes encoding (docs/SECURITY.md §3) with the ECDSA P-256
manifest-signing key, and writes the JSON the device's manifest parser
(components/svc_ota/ota_manifest_json.c) expects.

Reuses ota_manifest_oracle.canonical_bytes() rather than a third
reimplementation of the encoding -- exactly the drift Tier 0's
cross-language test vectors exist to catch would otherwise be free to
creep back in here, a place those vectors don't reach.

Uses `openssl dgst -sha256 -sign <key>` rather than a Python crypto
library: no new pip dependency, and it produces the same thing the C side
verifies -- openssl hashes the canonical bytes with SHA-256 internally and
signs that digest, yielding a standard DER ECDSA-Sig-Value, exactly what
mbedtls_pk_verify(ctx, MBEDTLS_MD_SHA256, hash, ...) expects on the device.

Release tooling, not a CI architecture gate -- the stdlib-only constraint
that governs run_all_checks.py's own gates doesn't apply here (this is
never run by `idf.py build`), but it still avoids a new dependency because
openssl is already required for the RSA image-signing key.

Usage:
    python tools/gen_manifest.py --image build/denon-spotify-controller.bin \\
        --url https://ota.example.com/dsc-0.2.0.bin --out manifest.json
"""

import argparse
import base64
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_manifest_oracle  # noqa: E402  (path insert must precede this)

DEFAULT_KEY_ID = "dsc-dev-2026-07"


def load_fw_version(path):
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    return data["version"], int(data["build"]), int(data["security_version"])


def sha256_and_size(image_path):
    h = hashlib.sha256()
    size = 0
    with open(image_path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def sign_canonical_bytes(canonical, key_path):
    proc = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", key_path],
        input=canonical, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        raise RuntimeError("openssl failed to sign the manifest: %s"
                            % proc.stderr.decode("utf-8", "replace"))
    return proc.stdout


def build_manifest(fw_version_path, image_path, url, key_path, key_id,
                    min_prev_build, mandatory, notes):
    version, build, security_version = load_fw_version(fw_version_path)
    sha256_hex, size = sha256_and_size(image_path)

    canonical = ota_manifest_oracle.canonical_bytes(
        version, build, security_version, url, sha256_hex, size,
        key_id, min_prev_build, mandatory, notes)
    sig_der = sign_canonical_bytes(canonical, key_path)

    return {
        "version": version,
        "build": build,
        "security_version": security_version,
        "url": url,
        "sha256": sha256_hex,
        "size": size,
        "sig": base64.b64encode(sig_der).decode("ascii"),
        "key_id": key_id,
        "min_prev_build": min_prev_build,
        "mandatory": mandatory,
        "notes": notes,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw-version", default=None, help="path to fw_version.json")
    ap.add_argument("--image", required=True, help="path to the built firmware .bin")
    ap.add_argument("--url", required=True, help="the URL the device will fetch the image from")
    ap.add_argument("--key", default=None, help="ECDSA P-256 manifest-signing key PEM")
    ap.add_argument("--key-id", default=DEFAULT_KEY_ID)
    ap.add_argument("--min-prev-build", type=int, default=0)
    ap.add_argument("--mandatory", action="store_true")
    ap.add_argument("--notes", default="")
    ap.add_argument("--out", default=None, help="output manifest.json path")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    fw_version_path = args.fw_version or os.path.join(root, "fw_version.json")
    key_path = args.key or os.path.join(root, "tools", "keys", "dev_manifest_signing.pem")
    out_path = args.out or os.path.join(root, "manifest.json")

    if not os.path.exists(args.image):
        print("MISSING image: %s" % args.image)
        return 1
    if not os.path.exists(key_path):
        print("MISSING signing key: %s (see tools/keys/README.md)" % key_path)
        return 1

    manifest = build_manifest(fw_version_path, args.image, args.url, key_path,
                               args.key_id, args.min_prev_build, args.mandatory, args.notes)

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(manifest, fh, ensure_ascii=False, indent=2)
        fh.write("\n")

    print("wrote %s (build=%d security_version=%d size=%d sha256=%s...)"
          % (out_path, manifest["build"], manifest["security_version"],
             manifest["size"], manifest["sha256"][:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
