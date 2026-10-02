# asset

**Zweck:** Laden, Cachen und Verwalten aller Inhalte.

## Bestandteile
- **VFS**: Mount-Liste aus Ordnern und `.g7pak`-Archiven; höhere Priorität überschreibt
  (Mods/Patches, wie Gothics VDF-Mechanik). Pfade case-insensitive, `/` als Trenner.
- **Asset-Handles**: `Handle<T>` mit Referenzzählung; `AssetManager::load<T>(path)` liefert
  sofort ein Handle, Laden läuft asynchron; `isReady()`, Platzhalter bis fertig.
- **Loader-Registry**: pro Typ (`Texture`, `Mesh`, `Skeleton`, `AnimationClip`, `Sound`, `WorldData`, `Material`).
- **Hot-Reload**: Dateiüberwachung im Entwicklungsmodus → Loader lädt neu, Handle bleibt gültig.

## .g7pak-Format (Entwurf)
```
Header { magic "G7PK", version u32, entryCount u32, tocOffset u64 }
Daten  { Blöcke, optional LZ4/Zstd-komprimiert }
TOC    { pfadHash u64, pfad (UTF-8), offset u64, size u64, rawSize u64, flags u32 }
```

## Geplante API
```cpp
namespace g7::asset {
class Vfs { public: Result<void> mount(std::string_view source, i32 priority);
                    Result<std::vector<u8>> read(std::string_view path) const; bool exists(...) const; };
template <class T> class Handle { ... const T* get() const; bool isReady() const; };
class AssetManager { public: template <class T> Handle<T> load(std::string_view path); void update(); };
}
```
