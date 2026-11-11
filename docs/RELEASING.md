# Releasing

- [First publication](#first-publication)
- [Versions](#versions)
- [A release](#a-release)
- [Maven Central](#maven-central)
- [The download mirror](#the-download-mirror)
- [Licence obligations](#licence-obligations)

## First publication

In this order:

1. Create the repository on GitHub and push `main`.
2. Run `scripts/mirror-upload.sh` from a checkout that has run `./build.sh fetch` and
   `scripts/sources.sh`. It creates the `pinned-downloads` prerelease with every pinned file.
   Until it exists, the build fails as soon as Termux has moved one of the pinned `.deb`
   files.
3. Expect a green build on `main`; re-run it if it started before step 2 and failed in
   `fetch`.
4. Set up Maven Central (below), then tag the first release.

## Versions

`<biber version>-<build>`: `2.21-1` is the first build of biber 2.21, `2.21-2` the next one
with the same biber. The tag is the version with a `v` in front (`v2.21-1`). Untagged builds
are `2.21-dev`.

## A release

Before tagging, add the release to [CHANGELOG.md](../CHANGELOG.md) and update the version
shown in the README. If the library's public API changed on purpose, `android/biberdroid/api/biberdroid.api`
must already be updated (see [BUILDING.md](BUILDING.md#the-android-library)); CI fails
otherwise.

Push a tag:

```sh
git tag -a v2.21-2 -m "biberdroid 2.21-2"
git push origin v2.21-2
```

`.github/workflows/build.yml` then builds and checks the payload, builds the AAR, and
publishes a GitHub release with these files:

| File | Contents |
| --- | --- |
| `biberdroid-<version>-android-arm64.tar.gz` | `jniLibs/`, `assets/biber/`, `manifest.tsv`, `COMPONENTS.tsv`, `FILES.tsv`, `NOTICES.txt` |
| `biberdroid-<version>-jniLibs-arm64-v8a.tar.gz` | `jniLibs/arm64-v8a/*.so` |
| `biberdroid-<version>-perl-tree.tar.xz` | The Perl tree, as the library unpacks it |
| `biberdroid-<version>.aar` | The Android library |
| `biberdroid-<version>-sources.tar` | The complete source of the payload (`scripts/sources.sh`) |
| `NOTICES.txt`, `COMPONENTS.tsv` | Licences and sources of every component |
| `SHA256SUMS` | Checksums of the files above |

## Maven Central

Two jobs handle Maven Central, `io.github.lavishsonbhurra:biberdroid:<version>`. Both run
only on a tag and only when all four secrets below are set; otherwise they note that they
skipped. `sign` runs before the GitHub release is created: it builds the publication again
with Gradle signing, checks that its AAR is byte-identical to the one going into the release,
and packs the bundle, so a mismatch stops the tag before anything is published. `central`
runs after the release and uploads the bundle to the Central Portal as `USER_MANAGED`. The
deployment stops at VALIDATED: nothing is public until someone presses Publish at
<https://central.sonatype.com/publishing/deployments>, which cannot be undone.

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
  ./gradlew :biberdroid:publishReleasePublicationToStagingRepository
python3 -I ../scripts/central-bundle.py --repo build/central-staging --out central-bundle.zip
```

Gradle also reads the key from the properties `signingInMemoryKey` and
`signingInMemoryKeyPassword`. Without a key the publication is written unsigned, which
`central-bundle.py` refuses.

## The download mirror

After the first push, and whenever a pin changes, copy the pinned downloads to the
`pinned-downloads` prerelease so that the build can still run after Termux removes the old
packages:

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
  the AAR carries it as `assets/biberdroid/NOTICES.txt` (`Biber.notices(context)`), so an
  app can show it.
- **Source.** `biberdroid-<version>-sources.tar`, attached to the same release as the
  binaries, holds biber and every CPAN distribution as downloaded, the upstream sources of
  Perl (with perl-cross), libxml2, zlib and libiconv, Termux's recipes and patches for them
  at the pinned commit, and this repository. This is the source offered from the same place
  as the object code, as the GNU LGPL 2.1 (section 6) asks for libiconv, and it covers the
  Artistic License's terms for the Termux build of Perl.
- **Replacing libiconv.** libiconv stays a separate shared library, `libiconv.so`, loaded by
  name, so a user can rebuild it from that source and replace it, as the LGPL requires.
- **Unchanged sources.** biber, Perl and the CPAN modules are not modified; the only changes
  to binaries are the patchelf edits listed in `scripts/package.py`, which `NOTICES.txt`
  states. This project's own `BiberNative.pm` answers one ExtUtils::MakeMaker method
  (`MM->maybe_command`) in place of loading MakeMaker; MakeMaker's files are untouched.

An app that uses the library should add `NOTICES.txt` to its licences screen, and keep a
pointer to the release's sources file (or ship the sources itself) for libiconv.
