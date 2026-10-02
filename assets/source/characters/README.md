# Figuren und Animationen (Quell-Assets)

Zuständig: Figuren-Spur (`docs/coordination.md`). Pipeline und Konventionen:
`docs/design/characters-pipeline.md`, Skelett-Vertrag: `docs/modules/animation.md` („Referenz-Skelett“).

```
rig/human_reference.blend   Referenz-Rig (Quelle der Wahrheit), erzeugt mit `gothar-chargen build-rig`
rig/human_reference.glb     Export davon; Bind-Pose-Referenz für den Validator, Testfigur für die Engine
figures/                    Figuren (Körper, Köpfe, Kleidung) als .blend + .glb          (ab F1/F3)
anims/human/<set>.glb       Animations-Sets je Modus, dazu <set>.events.toml             (ab F1/F2)
```

Jede `.glb` hier wird in CI mit `gothar-chargen validate --strict` geprüft (`tools/chargen`).
Fremdquellen → `assets/LICENSES.md`. Keine Figuren oder Animationen aus Gothic.
