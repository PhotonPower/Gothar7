// The hero as an animated figure (M6 part C): loads the skinned model ([game] hero, falling back to the
// placeholder mannequin), drives the animation state machine with the movement of each fixed step and
// draws it with GPU skinning. Climbing follows the climb clip's root motion, scaled to the ledge.

#include "PlayerFigure.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/core/Log.hpp>
#include <g7/runtime/AssetMounts.hpp>
#include <g7/runtime/Engine.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr std::string_view kHero = "characters/figures/farmer.glb";
constexpr std::string_view kFallbackHero = "characters/figures/placeholder_mannequin.glb";
constexpr std::string_view kHeroGraph = "data/anim/human.animgraph.toml";
constexpr usize kShownEvents = 8;
constexpr std::array<std::string_view, 3> kClimbStates = {"climb_low", "climb_mid", "climb_high"};

usize ledgeIndex(gameplay::LedgeClass ledge)
{
    return static_cast<usize>(ledge);
}
} // namespace

Result<std::unique_ptr<PlayerFigure>> Engine::loadFigure(std::string_view path, std::string_view graphPath)
{
    auto figure = std::make_unique<PlayerFigure>();
    figure->path = std::string(path);
    figure->graphPath = std::string(graphPath);
    auto graphBytes = m_vfs.read(graphPath);
    if (!graphBytes)
    {
        return Error{std::format("{}: {}", graphPath, graphBytes.error().message)};
    }
    auto graph = animation::AnimGraph::parse(
        std::string_view(reinterpret_cast<const char*>(graphBytes.value().data()), graphBytes.value().size()),
        graphPath);
    if (!graph)
    {
        return graph.error();
    }
    const auto model = m_assets->load<asset::SkinnedModelData>(path);
    std::vector<asset::Handle<asset::AnimationSetData>> setHandles;
    for (const std::string& set : graph.value().sets)
    {
        setHandles.push_back(m_assets->load<asset::AnimationSetData>(set));
    }
    m_assets->waitAll();
    if (model.failed() || !model.get())
    {
        return Error{std::format("{}: {}", path, model.error())};
    }
    std::vector<const asset::AnimationSetData*> sets;
    for (usize i = 0; i < setHandles.size(); ++i)
    {
        if (setHandles[i].failed() || !setHandles[i].get())
        {
            return Error{std::format("{}: {}", graph.value().sets[i], setHandles[i].error())};
        }
        sets.push_back(setHandles[i].get());
    }
    const asset::SkinnedModelData& data = *model.get();
    auto skeleton = animation::Skeleton::create(data.skeleton);
    if (!skeleton)
    {
        return Error{std::format("{}: {}", path, skeleton.error().message)};
    }
    figure->skeleton = std::move(skeleton).value();
    auto animator = animation::Animator::create(graph.value(), figure->skeleton, sets);
    if (!animator)
    {
        return Error{std::format("{} with {}: {}", graphPath, path, animator.error().message)};
    }
    figure->animator = std::move(animator).value();
    figure->startState = graph.value().start;
    figure->inverseBind = data.inverseBind;
    figure->modelSpace.resize(figure->skeleton.size());
    figure->bones.assign(figure->skeleton.size(), Mat4(1.0f));
    figure->face =
        animation::FaceAnimator(static_cast<u32>(std::hash<std::string_view>{}(path)), graph.value().face);
    if (auto look = animation::LookAt::create(figure->skeleton, graph.value().lookAt))
    {
        figure->lookAt = std::move(look).value();
    }
    else
    {
        G7_LOG_WARN("engine", "{}: {} - the head does not turn", path, look.error().message);
    }
    for (usize bone = 0; bone < figure->skeleton.size(); ++bone)
    {
        if (figure->skeleton.name(bone).starts_with("socket_"))
        {
            figure->sockets.push_back(figure->skeleton.name(bone));
        }
    }

    // Climb clips: how far their root moves, to scale it to the ledge found.
    for (usize ledge = 0; ledge < kClimbStates.size(); ++ledge)
    {
        const auto state =
            std::find_if(graph.value().states.begin(), graph.value().states.end(),
                         [&](const animation::AnimGraphState& s) { return s.name == kClimbStates[ledge]; });
        if (state == graph.value().states.end() || !state->rootMotion || state->points.empty())
        {
            continue;
        }
        for (const asset::AnimationSetData* set : sets)
        {
            if (const asset::ClipData* clipData = set->find(state->points.front().second))
            {
                const animation::Clip clip(*clipData, figure->skeleton);
                figure->climbRoot[ledge] = clip.rootTranslation(clip.duration()) - clip.rootTranslation(0.0f);
                figure->climbSeconds[ledge] = clip.duration() / std::max(state->speed, 0.01f);
                break;
            }
        }
    }

    // Textures next to the figure (../textures/...), then the GPU side.
    figure->images.assign(data.images.size(), {});
    for (usize i = 0; i < data.images.size(); ++i)
    {
        if (data.images[i].uri.empty())
        {
            continue;
        }
        const auto candidates = imageCandidates(path, data.images[i].uri);
        const auto found = std::find_if(candidates.begin(), candidates.end(),
                                        [&](const std::string& c) { return m_vfs.exists(c); });
        if (found == candidates.end())
        {
            G7_LOG_WARN("engine", "{}: image '{}' not found in the VFS", path, data.images[i].uri);
            continue;
        }
        figure->images[i] = m_assets->load<asset::TextureData>(*found);
    }
    m_assets->waitAll();
    if (m_device)
    {
        auto mesh = render::SkinnedMesh::create(*m_device, data, 0);
        if (!mesh)
        {
            return Error{std::format("{}: {}", path, mesh.error().message)};
        }
        asset::MeshData materialData; // MaterialSet reads only materials and images
        materialData.materials = data.materials;
        materialData.images = data.images;
        auto materials = render::MaterialSet::create(
            *m_device, materialData,
            [&](const asset::ImageSource& source) -> const asset::TextureData*
            {
                const auto index = static_cast<usize>(&source - materialData.images.data());
                const asset::Handle<asset::TextureData>& image = figure->images[index];
                if (image.failed() && image.valid())
                {
                    G7_LOG_WARN("engine", "{}: {}", path, image.error());
                }
                return image.get();
            },
            m_meshRenderer.defaults());
        if (!materials)
        {
            return Error{std::format("{}: {}", path, materials.error().message)};
        }
        figure->mesh = std::move(mesh).value();
        figure->materials = std::move(materials).value();
        figure->uploaded = true;
    }
    G7_LOG_INFO("engine", "player figure {}: {} bones, {} parts, animation {} ({} sets)", path,
                figure->skeleton.size(), data.parts.size(), graphPath, sets.size());
    return figure;
}

void Engine::loadPlayerFigure()
{
    if (m_figure)
    {
        return;
    }
    const std::string hero = m_config.settings.get<std::string>("game.hero", std::string(kHero));
    const std::string graph = m_config.settings.get<std::string>("game.hero_graph", std::string(kHeroGraph));
    for (const std::string& path : {hero, std::string(kFallbackHero)})
    {
        auto figure = loadFigure(path, graph);
        if (figure)
        {
            m_figure = std::move(figure).value();
            return;
        }
        G7_LOG_WARN("engine", "player figure: {}", figure.error().message);
        if (path == kFallbackHero)
        {
            break;
        }
    }
    G7_LOG_WARN("engine", "no animated player figure - the debug overlay (F2) shows the capsule");
}

std::string_view Engine::playerAnimationState() const noexcept
{
    return m_figure ? m_figure->animator.state() : std::string_view();
}

std::string_view Engine::playerFigurePath() const noexcept
{
    return m_figure ? std::string_view(m_figure->path) : std::string_view();
}

void Engine::resetPlayerAnimation()
{
    if (!m_figure)
    {
        return;
    }
    m_figure->animator.enter(m_figure->startState);
    m_figure->fallSeconds = 0.0f;
    m_figure->airSeconds = 0.0f;
    m_figure->jumped = false;
    m_figure->climbMoved = Vec3(0.0f);
    m_figure->poseNow = m_figure->posePrevious = m_figure->animator.pose();
    m_figure->drawnFrame = ~u64(0);
    preparePlayerPose(1.0f);
}

Mat4 Engine::playerFigureTransform() const
{
    // Feet interpolated between the last two fixed steps; models face +Z, yaw 0 looks along -Z: half a turn
    // more.
    const f32 alpha = static_cast<f32>(m_fixedStep.alpha());
    const Vec3 feet = glm::mix(m_playerFeetBefore, m_playerFeet, alpha);
    return glm::translate(Mat4(1.0f), feet) *
           glm::rotate(Mat4(1.0f), m_movement.yaw() + glm::pi<f32>(), Vec3(0, 1, 0));
}

void Engine::preparePlayerPose(f32 alpha)
{
    // Once per frame (the shadow cascades and the main pass share it): the pose between the last two steps.
    PlayerFigure& f = *m_figure;
    if (f.drawnFrame == m_frameCount)
    {
        return;
    }
    f.drawnFrame = m_frameCount;
    f.poseDrawn = f.posePrevious;
    animation::blendPose(f.poseDrawn, f.poseNow, std::clamp(alpha, 0.0f, 1.0f));
    f.skeleton.modelSpace(f.poseDrawn, f.modelSpace);
    for (usize i = 0; i < f.bones.size(); ++i)
    {
        f.bones[i] = f.modelSpace[i] * f.inverseBind[i];
    }
    if (f.uploaded)
    {
        f.mesh.setMorphWeights(f.face.weights()); // uploads only when a weight changed
    }
}

Result<void> Engine::attachToPlayer(std::string_view socket, std::string_view modelPath)
{
    if (!m_figure)
    {
        return Error{"no animated player figure"};
    }
    if (auto loaded = loadModels({std::string(modelPath)}); !loaded)
    {
        return loaded;
    }
    return attachModel(socket, model(modelPath), nullptr);
}

Result<void> Engine::attachToPlayer(std::string_view socket, const asset::MeshData& mesh,
                                    std::string_view name)
{
    if (!m_figure)
    {
        return Error{"no animated player figure"};
    }
    auto owned = std::make_unique<LoadedModel>();
    owned->name = std::string(name);
    owned->bounds = mesh.bounds;
    if (m_device)
    {
        auto gpuMesh = render::Mesh::create(*m_device, *m_geometry, mesh);
        auto materials = render::MaterialSet::create(*m_device, mesh, render::MaterialSet::ImageLookup{},
                                                     m_meshRenderer.defaults());
        if (!gpuMesh || !materials)
        {
            return Error{std::format("cannot upload {}", name)};
        }
        owned->mesh = std::move(gpuMesh).value();
        owned->materials = std::move(materials).value();
    }
    const LoadedModel* model = owned.get();
    return attachModel(socket, model, std::move(owned));
}

Result<void> Engine::attachModel(std::string_view socket, const LoadedModel* model,
                                 std::unique_ptr<LoadedModel> owned)
{
    const i32 bone = m_figure->skeleton.find(socket);
    if (bone < 0)
    {
        return Error{std::format("{} has no bone '{}'", m_figure->path, socket)};
    }
    detachFromPlayer(socket); // one model per socket
    FigureAttachment attachment;
    attachment.socket = std::string(socket);
    attachment.bone = static_cast<usize>(bone);
    attachment.model = model;
    attachment.owned = std::move(owned);
    m_figure->attachments.push_back(std::move(attachment));
    G7_LOG_INFO("engine", "player: {} at {}", model->name, socket);
    return {};
}

void Engine::detachFromPlayer(std::string_view socket)
{
    if (m_figure)
    {
        std::erase_if(m_figure->attachments, [&](const FigureAttachment& a) { return a.socket == socket; });
    }
}

std::optional<Mat4> Engine::playerSocketTransform(std::string_view socket) const
{
    if (!m_figure || !m_player.valid())
    {
        return std::nullopt;
    }
    const i32 bone = m_figure->skeleton.find(socket);
    if (bone < 0)
    {
        return std::nullopt;
    }
    // The pose of the last fixed step (no interpolation: the same in headless runs).
    std::vector<Mat4> modelSpace(m_figure->skeleton.size());
    m_figure->skeleton.modelSpace(m_figure->poseNow, modelSpace);
    const Mat4 figure = glm::translate(Mat4(1.0f), m_playerFeet) *
                        glm::rotate(Mat4(1.0f), m_movement.yaw() + glm::pi<f32>(), Vec3(0, 1, 0));
    return figure * modelSpace[static_cast<usize>(bone)];
}

void Engine::setPlayerLookTarget(std::optional<Vec3> target)
{
    if (m_figure)
    {
        m_figure->lookTarget = target;
    }
}

bool Engine::setPlayerExpression(std::string_view name, f32 weight)
{
    return m_figure && m_figure->face.setExpression(name, weight);
}

void Engine::setPlayerTalking(bool talking)
{
    if (m_figure)
    {
        m_figure->face.setTalking(talking);
    }
}

std::span<const f32> Engine::playerFaceWeights() const noexcept
{
    return m_figure ? m_figure->face.weights() : std::span<const f32>();
}

f32 Engine::playerLookYawDegrees() const noexcept
{
    return m_figure ? m_figure->lookAt.yawDegrees() : 0.0f;
}

f32 Engine::climbDuration(gameplay::LedgeClass ledge) const
{
    const f32 animated = m_figure ? m_figure->climbSeconds[ledgeIndex(ledge)] : 0.0f;
    return animated > 0.0f ? animated : gameplay::climbSeconds(ledge, m_movementSettings.climb);
}

void Engine::startClimb(const Vec3& from, const Vec3& to, gameplay::LedgeClass ledge)
{
    m_climb = gameplay::ClimbPath{from, to, climbDuration(ledge), ledge};
    m_climbSeconds = 0.0f;
    if (m_figure)
    {
        m_figure->climbMoved = Vec3(0.0f);
    }
}

Vec3 Engine::climbPosition() const
{
    const usize ledge = ledgeIndex(m_climb->ledge);
    const Vec3 total = m_figure ? m_figure->climbRoot[ledge] : Vec3(0.0f);
    if (!m_figure || m_figure->climbSeconds[ledge] <= 0.0f || total.y < 0.05f || total.z < 0.05f)
    {
        return m_climb->at(m_climbSeconds); // no climb clip: the scripted path (M5)
    }
    // The clip's root moves up and ahead (+Z); scaled so that its end is the ledge's standing point.
    const f32 up = std::clamp(m_figure->climbMoved.y / total.y, 0.0f, 1.0f);
    const f32 ahead = std::clamp(m_figure->climbMoved.z / total.z, 0.0f, 1.0f);
    const Vec3 rise = m_climb->to - m_climb->from;
    return m_climb->from + Vec3(rise.x * ahead, rise.y * up, rise.z * ahead);
}

void Engine::animatePlayer(f32 seconds, const gameplay::MoveInput& input)
{
    if (!m_figure || seconds <= 0.0f)
    {
        return;
    }
    PlayerFigure& f = *m_figure;
    animation::Animator& a = f.animator;
    // Speeds from the movement drawn (all modes alike: walking, swimming, sliding).
    const Vec3 moved = (m_playerFeet - m_playerFeetBefore) / seconds;
    const Vec3 forward = gameplay::forwardOf(m_movement.yaw());
    const Vec3 right(-forward.z, 0.0f, forward.x);
    const gameplay::WaterMode water = m_swimmer.mode();
    const physics::MoveState state = m_player.state();
    const bool onLand = water == gameplay::WaterMode::Land && !m_climb;
    const bool air = onLand && state == physics::MoveState::Air;
    f.airSeconds = air ? f.airSeconds + seconds : 0.0f;
    f.fallSeconds = air && moved.y < 0.0f ? f.fallSeconds + seconds : 0.0f;
    const bool landed = m_lastLanding && m_lastLanding->tick == m_simTicks;

    a.setFloat("speed", m_climb ? 0.0f : moved.x * forward.x + moved.z * forward.z);
    a.setFloat("strafe", m_climb ? 0.0f : moved.x * right.x + moved.z * right.z);
    a.setFloat("turn", onLand ? input.turn : 0.0f);
    a.setBool("jump", f.jumped);
    f.jumped = false;
    a.setFloat("air", f.airSeconds);
    a.setFloat("fall", f.fallSeconds);
    a.setFloat("landed", landed ? std::max(m_lastLanding->height, 0.01f) : 0.0f);
    a.setBool("hard", landed && m_lastLanding->damage > 0.0f);
    a.setFloat("climb", m_climb ? static_cast<f32>(ledgeIndex(m_climb->ledge) + 1) : 0.0f);
    a.setBool("swim", water == gameplay::WaterMode::Swim);
    a.setBool("dive", water == gameplay::WaterMode::Dive);
    a.setBool("slide", onLand && state == physics::MoveState::Slide);
    a.setBool("sneak", onLand && input.sneak);
    a.update(seconds,
             [&](std::string_view clip, std::string_view event)
             {
                 f.events.push_front(std::format(
                     "{:.2f}  {}  {}", static_cast<f64>(m_simTicks) * m_fixedStep.step(), clip, event));
                 if (f.events.size() > kShownEvents)
                 {
                     f.events.pop_back();
                 }
                 G7_LOG_DEBUG("engine", "animation event {} ({})", event, clip);
             });
    if (m_climb)
    {
        f.climbMoved += a.rootMotion();
    }

    // Head towards the look target (world -> the figure's model space at this step), then the face.
    f.posePrevious = std::move(f.poseNow);
    f.poseNow = a.pose();
    if (f.lookAt.valid())
    {
        const std::optional<Vec3> world =
            f.lookAtCamera ? std::optional<Vec3>(m_camera.transform.position) : f.lookTarget;
        if (world)
        {
            const Mat4 figure = glm::translate(Mat4(1.0f), m_playerFeet) *
                                glm::rotate(Mat4(1.0f), m_movement.yaw() + glm::pi<f32>(), Vec3(0, 1, 0));
            f.lookAt.setTarget(Vec3(glm::inverse(figure) * Vec4(*world, 1.0f)));
        }
        else
        {
            f.lookAt.setTarget(std::nullopt);
        }
        f.lookAt.update(seconds, f.skeleton, f.poseNow);
    }
    f.face.update(seconds);
}

bool Engine::drawPlayerFigure(const Mat4& transform, bool shadow, u32 cascade)
{
    if (!m_figure || !m_figure->uploaded)
    {
        return false;
    }
    preparePlayerPose(static_cast<f32>(m_fixedStep.alpha()));
    const PlayerFigure& f = *m_figure;
    if (shadow)
    {
        m_meshRenderer.drawShadowSkinned(*m_device, f.mesh, f.materials, transform, f.bones,
                                         m_cascades[cascade]);
    }
    else
    {
        m_meshRenderer.drawSkinned(*m_device, f.mesh, f.materials, transform, f.bones, m_camera);
    }
    for (const FigureAttachment& attachment : f.attachments)
    {
        const Mat4 at = transform * f.modelSpace[attachment.bone];
        if (shadow)
        {
            m_meshRenderer.drawShadow(*m_device, attachment.model->mesh, attachment.model->materials, at,
                                      m_cascades[cascade]);
        }
        else
        {
            m_meshRenderer.draw(*m_device, attachment.model->mesh, attachment.model->materials, at, m_camera);
        }
    }
    return true;
}

void Engine::drawPlayerSockets()
{
    if (!m_figure || !m_figure->showSockets)
    {
        return;
    }
    // Axes of every socket: X red, Y (grip axis) green, Z blue, 10 cm.
    preparePlayerPose(static_cast<f32>(m_fixedStep.alpha()));
    const Mat4 figure = playerFigureTransform();
    for (const std::string& socket : m_figure->sockets)
    {
        const Mat4 at = figure * m_figure->modelSpace[static_cast<usize>(m_figure->skeleton.find(socket))];
        const Vec3 origin(at[3]);
        m_debugDraw.line(origin, origin + glm::normalize(Vec3(at[0])) * 0.1f, {Vec4(1, 0.2f, 0.2f, 1)});
        m_debugDraw.line(origin, origin + glm::normalize(Vec3(at[1])) * 0.1f, {Vec4(0.2f, 1, 0.2f, 1)});
        m_debugDraw.line(origin, origin + glm::normalize(Vec3(at[2])) * 0.1f, {Vec4(0.3f, 0.5f, 1, 1)});
    }
}

void Engine::playerAnimationUi()
{
    if (!m_figure || !m_player.valid())
    {
        return;
    }
    const animation::Animator& a = m_figure->animator;
    ui::AnimationPanel panel;
    panel.figure = m_figure->path;
    panel.graph = m_figure->graphPath;
    panel.state = std::string(a.state());
    panel.previousState = std::string(a.previousState());
    panel.fade = a.fadeWeight();
    panel.progress = a.stateProgress();
    panel.rate = a.playbackRate();
    for (const animation::Animator::ClipWeight& clip : a.activeClips())
    {
        panel.clips.emplace_back(std::string(clip.clip), clip.weight);
    }
    for (const char* name : {"speed", "strafe", "turn", "jump", "air", "fall", "landed", "hard", "climb",
                             "swim", "dive", "slide", "sneak"})
    {
        panel.params.emplace_back(name, a.param(name));
    }
    panel.events.assign(m_figure->events.begin(), m_figure->events.end());
    // Trying things out: sockets, a test stick, expressions, talking, look-at.
    PlayerFigure& f = *m_figure;
    panel.sockets = f.sockets;
    panel.showSockets = f.showSockets;
    panel.stickSocket = f.stickSocket;
    panel.expression = std::string(f.face.expression());
    panel.expressionWeight = f.face.expression().empty() ? 1.0f : m_figureExpressionWeight;
    panel.talking = f.face.talking();
    panel.lookAtCamera = f.lookAtCamera;
    panel.lookYaw = f.lookAt.yawDegrees();
    panel.lookPitch = f.lookAt.pitchDegrees();
    m_debugUi.animationPanel(panel);

    f.showSockets = panel.showSockets;
    f.lookAtCamera = panel.lookAtCamera;
    f.face.setTalking(panel.talking);
    if (panel.expression != f.face.expression() || panel.expressionWeight != m_figureExpressionWeight)
    {
        m_figureExpressionWeight = panel.expressionWeight;
        f.face.setExpression(panel.expression, panel.expressionWeight);
    }
    if (panel.stickSocket != f.stickSocket)
    {
        if (!f.stickSocket.empty())
        {
            detachFromPlayer(f.stickSocket);
        }
        f.stickSocket = panel.stickSocket;
        if (!f.stickSocket.empty())
        {
            // A 0.9 m stick along the socket's grip axis (+Y), held a quarter from its lower end.
            asset::MeshData stick =
                asset::makeBox(Vec3(0.015f, 0.45f, 0.015f), Vec4(0.45f, 0.3f, 0.15f, 1.0f));
            for (asset::Vertex& v : stick.vertices)
            {
                v.position.y += 0.2f;
            }
            stick.bounds = AABB{stick.bounds.min + Vec3(0, 0.2f, 0), stick.bounds.max + Vec3(0, 0.2f, 0)};
            if (auto attached = attachToPlayer(f.stickSocket, stick, "debug stick"); !attached)
            {
                G7_LOG_WARN("engine", "test stick: {}", attached.error().message);
                f.stickSocket.clear();
            }
        }
    }
}
} // namespace g7
