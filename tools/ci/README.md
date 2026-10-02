# CI-Workflow (noch zu aktivieren)

`ci.yml` ist der GitHub-Actions-Workflow (Windows + Linux: Build, Tests, Smoke-Test).
Er liegt hier, weil der beim Anlegen verwendete Token keine `workflow`-Berechtigung hatte.

Aktivieren:
```bash
mkdir -p .github/workflows && git mv tools/ci/ci.yml .github/workflows/ci.yml
git commit -m "ci: enable GitHub Actions workflow" && git push
```
(mit deinem normalen Git-Login oder einem Token mit Berechtigung „Workflows: Read and write“).
Danach kann dieser Ordner gelöscht werden.
