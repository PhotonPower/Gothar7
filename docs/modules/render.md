# render

**Zweck:** Darstellung. OpenGL 4.6 Core (ADR 0003) hinter einer dünnen RHI-Schicht, damit
später ein Vulkan-Backend möglich bleibt.

## Schichten
1. **RHI** (`render/rhi/`): `Buffer`, `Texture`, `Sampler`, `ShaderProgram`, `PipelineState`,
   `Framebuffer`, `CommandList` (zunächst direkt ausgeführt). RAII-Wrapper um GL-Objekte, DSA-Stil.
2. **Renderer**: `Mesh`, `Material`, `Camera`, `Light`, `RenderScene` (Liste sichtbarer Objekte, von `world` befüllt), Passes.
3. **Features**: Himmel, Schatten, Nebel, Partikel (M12), Wasser/Post (M17), Debug-Draw, ImGui-Backend.

## Frame-Ablauf (Ziel)
```
cull (frustum + distance) → shadow pass (CSM, 3–4 Kaskaden) → depth prepass
→ opaque (forward+, clustered point lights) → alpha-tested (vegetation) → sky
→ transparent (particles, water) → post (fog, tonemap, bloom) → UI
```

## Stil-Ziel
Gothic lebt von Stimmung, nicht Realismus: starker **Distanznebel** passend zur Himmelsfarbe,
warme Punktlichter (Fackeln, Feuer) mit Flackern, dunkle Nächte, farbige Tageszeiten.
Wichtig ist ein **zeitabhängiges Farbschema** (Himmel, Nebel, Ambient, Sonne) als Daten-Kurve.

## Geplante API (Ausschnitt)
```cpp
namespace g7::render {
class Renderer {
public:
    Result<void> init(platform::Window&, const RenderConfig&);
    void beginFrame(const Camera&, const Environment&);   // Environment: sun dir/color, ambient, fog, sky colors
    void submit(const MeshDrawItem&);                     // mesh, material, world matrix, skinning palette (opt.)
    void submitLight(const PointLight&);
    void endFrame();                                      // executes passes, presents
    DebugDraw& debug();
};
}
```

## Offene Fragen
- Forward+ vs. Clustered Forward: beginnen mit einfachem Forward + Light-Limit pro Objekt, umstellen wenn > 32 Lichter sichtbar.
- Bindless-Texturen (`GL_ARB_bindless_texture`) optional.
