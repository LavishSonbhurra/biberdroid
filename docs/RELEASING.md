# Releasing

- [Versions](#versions)
- [A release](#a-release)
- [Maven Central](#maven-central)
- [The download mirror](#the-download-mirror)
- [Licence obligations](#licence-obligations)

## Versions

`<biber version>-<build>`: `2.21-1` is the first build of biber 2.21, `2.21-2` the next one
with the same biber. The tag is the version with a `v` in front (`v2.21-1`). Untagged builds
are `2.21-dev`.

## A release

Push a tag:

```sh
git tag -a v2.21-1 -m "biber-android 2.21-1"
git push origin v2.21-1
```

`.github/workflows/build.yml` then builds and checks the payload, builds the AAR, and
publishes a GitHub release with these files:

| File | Contents |
| --- | --- |
| `biber-android-<version>-android-arm64.tar.gz` | `jniLibs/`, `assets/biber/`, `manifest.tsv`, `COMPONENTS.tsv`, `FILES.tsv`, `NOTICES.txt` |
| `biber-android-<version>-jniLibs-arm64-v8a.tar.gz` | `jniLibs/arm64-v8a/*.so` |
| `biber-android-<version>-perl-tree.tar.xz` | The Perl tree, as the library unpacks it |
| `biber-android-<version>.aar` | The Android library |
| `biber-android-<version>-sources.tar` | The complete source of the payload (`scripts/sources.sh`) |
| `NOTICES.txt`, `COMPONENTS.tsv` | Licences and sources of every component |
| `SHA256SUMS` | Checksums of the files above |

## Maven Central

The `central` job uploads the AAR as `io.github.lavishsonbhurra:biber-android:<version>`. It
runs only on a tag, after the GitHub release, and only when all four secrets below are set;
otherwise it notes that it skipped the upload. It signs the publication with Gradle, checks
that the signed AAR is byte-identical to the one in the GitHub release, and uploads the bundle
to the Central Portal as `USER_MANAGED`. The deployment stops at VALIDATED: nothing is public
until someone presses Publish at <https://central.sonatype.com/publishing/deployments>, which
cannot be undone.

Set these in a GitHub environment named `maven-central`:

| Secret | Value |
| --- | --- |
| `CENTRAL_USERNAME`, `CENTRAL_PASSWORD` | A Central Portal user token ("Generate User Token"), not the account login |
| `SIGNING_KEY` | The signing key, ASCII armored (`gpg --armor --export-secret-keys <id>`) |
| `SIGNING_PASSPHRASE` | Its passphrase |

The namespace `io.github.lavishsonbhurra` must be verified in the Portal, and the key's public
half must be on a public key server.

Locally, the same publication is made with:

```sh
cd android
SIGNING_KEY="$(gpg --armor --export-secret-keys <id>)" SIGNING_PASSPHRASE=... \
  ./gradlew :biber:publishReleasePublicationToStagingRepository
python3 -I ../scripts/central-bundle.py --repo build/central-staging --out central-bundle.zip
```

Gradle also reads the key from the properties `signingInMemoryKey` and
`signingInMemoryKeyPassword`. Without a key the publication is written unsigned, which
`central-bundle.py` refuses.

## The download mirror

Before tagging a release with new pins, copy the pinned downloads to the `pinned-downloads`
release so that the build can still run after Termux removes the old packages:

```sh
./build.sh fetch && scripts/sources.sh && scripts/mirror-upload.sh --dry-run
scripts/mirror-upload.sh
```

It needs the GitHub CLI with write access, uploads only files the release does not have yet,
and never deletes any.

## Licence obligations

The release layout is designed so that whoever passes on a release, or an app that ships
the payload, can meet the licences of its components:

- **Notices.** `NOTICES.txt` lists every component with its version, licence and source, and
  holds every licence text once. It is a release file, it is inside the payload archive, and
  the AAR carries it as `assets/biber-android/NOTICES.txt` (`Biber.notices(context)`), so an
  app can show it.
- **Source.** `biber-android-<version>-sources.tar`, attached to the same release as the
  binaries, holds biber and every CPAN distribution as downloaded, the upstream sources of
  Perl (with perl-cross), libxml2, zlib and libiconv, Termux's recipes and patches for them
  at the pinned commit, and this repository. This is the source offered from the same place
  as the object code, as the GNU LGPL 2.1 (section 6) asks for libiconv, and it covers the
  Artistic License's terms for the Termux build of Perl.
- **Replacing libiconv.** libiconv stays a separate shared library, `libiconv.so`, loaded by
  name, so a user can rebuild it from that source and replace it, as the LGPL requires.
- **Unchanged sources.** biber, Perl and the CPAN modules are not modified; the only changes
  to binaries are the patchelf edits listed in `scripts/package.py`, which `NOTICES.txt`
  states.

An app that uses the library should add `NOTICES.txt` to its licences screen, and keep a
pointer to the release's sources file (or ship the sources itself) for libiconv.
