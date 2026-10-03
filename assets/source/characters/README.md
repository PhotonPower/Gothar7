# Figuren und Animationen (Quell-Assets)

Zuständig: Figuren-Spur (`docs/coordination.md`). Pipeline und Konventionen:
`docs/design/characters-pipeline.md`, Skelett-Vertrag: `docs/modules/animation.md` („Referenz-Skelett“).

```
rig/human_reference.blend   Referenz-Rig (Quelle der Wahrheit), erzeugt mit `gothar-chargen build-rig`
rig/human_reference.glb     Export davon; Bind-Pose-Referenz für den Validator, Testfigur für die Engine
figures/<name>.figure.toml  Figur-Manifest (Teile, LODs, Palette) → figures/<name>.glb  (`gothar-chargen assemble`)
figures/placeholder_*.blend Platzhalterfigur aus F1 (Quaternius-Mannequin, CC0)
parts/                      Figurteile (body/outfit, head, hair, beard) als .glb auf dem Referenz-Rig; parts/test = eigene Testteile
anims/human/<set>.glb       Animations-Sets je Modus, dazu <set>.events.toml             (ab F1/F2)
```

Jede `.glb` hier wird in CI mit `gothar-chargen validate --strict` geprüft (`tools/chargen`).
Fremdquellen → `assets/LICENSES.md`. Keine Figuren oder Animationen aus Gothic.
