# Maintainer release checklist

1. Update `VERSION`, both numeric/string versions in `app.rc`, `app.manifest`, version strings in `main.cpp` and `monitor.cpp`, and `CHANGELOG.md` in a reviewed pull request. Use `major.minor.patch`; do not reuse published tags.
2. Run `build.cmd`, `scripts\test.ps1`, and interactive visual checks. Record relevant manual hardware results without personal diagnostics. Check install/update/removal when those paths change.
3. Merge after Windows CI and CodeQL pass. Create and push an annotated tag such as `v1.0.3` pointing to that tested main commit.
4. The Release workflow checks tag/binary version agreement, rebuilds, tests, packages, generates checksums and GitHub provenance, creates a draft release, uploads all artifacts, and publishes it only after uploads succeed. No private signing key or personal token is required.
5. Verify the release assets, checksum, provenance, install instructions, and downloads. Amend the generated release description if context is needed. Publication is currently unsigned; do not imply Windows code signing.

Commands, after substituting the actual reviewed version:

```powershell
git switch main
git pull --ff-only
git tag -a v1.0.3 -m "Steady Brightness 1.0.3"
git push origin v1.0.3
```

If the workflow fails after creating a draft, inspect and repair that draft manually; the workflow does not overwrite existing releases or reuse tags. Build intermediates, downloaded binaries, and `dist/` must stay out of Git. GitHub automatically provides source archives for each tag.

Repository maintenance: review Dependabot updates for pinned actions, keep Issues and private vulnerability reporting enabled, preserve branch protections, and update support expectations as the project evolves. App runtime resource use is a release constraint.
