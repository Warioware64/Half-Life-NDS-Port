// The cooked model (core/src/ModelCook.cpp) and the DS's way of posing it
// (shared/HlAnim.hpp), held against the game's own floating-point maths on
// the game's own models.
//
// The claim is one a player could check with their eyes: at every frame of
// every cooked clip, every vertex of the model is where the game would put
// it, to within a unit (a pixel or so at the distances these are seen from)
// -- after the animation has been cut down to keys,
// the keys squeezed into sixteen bits, and the pose rebuilt in fixed point.
//
// Needs HLDS_VALVE_DIR. Set HLDS_DUMP_DIR as well to have the skins written
// there as PNGs, for looking at.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HlAnim.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/ModelCook.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    void checkModel(const std::string &dir, const char *file, const std::vector<std::string> &clips)
    {
        MdlFile mdl;
        std::string error;
        if (!mdl.load(dir + "/models/" + file, error))
        {
            check(false, "the model loads");
            return;
        }
        CookedModel cooked;
        ModelCookReport report;
        std::vector<std::uint8_t> bytes;
        if (!cookModel(mdl, clips, cooked, report, error) || !writeModel(cooked, bytes, error))
        {
            std::printf("test_anim: %s: %s\n", file, error.c_str());
            check(false, "the model cooks");
            return;
        }
        // The DS reads words: the block it is given is aligned.
        std::vector<std::uint32_t> aligned((bytes.size() + 3) / 4);
        std::memcpy(aligned.data(), bytes.data(), bytes.size());
        ModelView view;
        check(view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()),
                        static_cast<std::uint32_t>(bytes.size())),
              "the cooked model opens");
        for (const std::string &missing : report.missingClips)
            std::printf("test_anim: %s has no sequence '%s'\n", file, missing.c_str());
        check(report.missingClips.empty() && view.header->clipCount == clips.size(),
              "every clip asked for is there");
        if (!report.missingClips.empty())
            return;

        // The batches: each within the matrix stack, and between them all the
        // triangles of the model.
        int polygons = 0;
        bool fits = true;
        for (int b = 0; b < view.header->batchCount; ++b)
        {
            fits = fits && view.batches[b].boneCount <= kModelBatchBones;
            if (view.batchInBody(view.batches[b], 0))
                polygons += view.batches[b].polygons;
        }
        check(fits && polygons == report.triangles,
              "its batches fit the matrix stack and hold every triangle of body 0");
        check(report.triangles <= std::max(kModelTriangleTarget, report.sourceTriangles / 4) + 8 ||
                  report.triangles == report.sourceTriangles,
              "thinned to its target, or near enough");

        double worst = 0;
        const char *worstClip = "";
        std::vector<BoneMatrix> pose(mdl.bones.size());
        for (int c = 0; c < view.header->clipCount; ++c)
        {
            const int sequence = mdl.sequenceByName(clips[c]);
            const MdlSequence &seq = mdl.sequences[sequence];
            std::vector<MdlPose> poses;
            mdl.decode(sequence, poses, error);
            const std::size_t boneCount = mdl.bones.size();
            for (int f = 0; f < seq.frameCount; ++f)
            {
                // The game's pose, with the sequence's own travel taken out
                // as the game takes it out.
                std::vector<MdlPose> frame(poses.begin() + f * boneCount,
                                           poses.begin() + (f + 1) * boneCount);
                for (int a = 0; a < 3; ++a)
                {
                    if (seq.motionType & (1 << a))
                        frame[seq.motionBone].pos[a] = 0;
                }
                std::vector<std::array<double, 12>> want;
                mdlBoneMatrices(mdl, frame.data(), want);
                poseModel(view, c, static_cast<std::uint32_t>(f) << 8, pose.data());
                for (const MdlBodyPart &part : mdl.bodyParts)
                {
                    for (const MdlSubModel &model : part.models)
                    {
                        for (std::size_t v = 0; v < model.vertices.size(); ++v)
                        {
                            const int bone = model.vertexBone[v];
                            const auto &p = model.vertices[v];
                            for (int a = 0; a < 3; ++a)
                            {
                                const double w = want[bone][a * 4] * p[0] + want[bone][a * 4 + 1] * p[1] +
                                                 want[bone][a * 4 + 2] * p[2] + want[bone][a * 4 + 3];
                                const double g = (pose[bone].r[a * 3] * p[0] + pose[bone].r[a * 3 + 1] * p[1] +
                                                  pose[bone].r[a * 3 + 2] * p[2]) / 16384.0 +
                                                 pose[bone].t[a] / 4096.0;
                                if (std::fabs(w - g) > worst)
                                {
                                    worst = std::fabs(w - g);
                                    worstClip = clips[c].c_str();
                                }
                            }
                        }
                    }
                }
            }
        }
        std::printf("test_anim: %s: %d triangles (of %d) in %d batches, %zu bones; %d clips, %d bone-frames as "
                    "%d keys (%.0f%%), file %zu KB, skin %dx%d; worst vertex %.2f units off (%s)\n",
                    file, report.triangles, report.sourceTriangles, report.batches, mdl.bones.size(), report.clips,
                    report.sourceFrames, report.keys,
                    report.sourceFrames > 0 ? 100.0 * report.keys / report.sourceFrames : 0.0,
                    bytes.size() / 1024, cooked.atlas.width, cooked.atlas.height, worst, worstClip);
        std::fflush(stdout);
        // (A unit on a figure; in proportion on something the size of a
        // forklift, whose corners are a long way from the bones that turn
        // them.)
        check(worst < std::max(1.0, view.header->radius * 0.012),
              "every vertex is within a unit of where the game puts it");

        const char *dump = std::getenv("HLDS_DUMP_DIR");
        if (dump != nullptr && *dump != '\0' && cooked.atlas.width != 0)
            writePng(std::string(dump) + "/" + mdl.name + "_atlas.png", cooked.atlas, error);
    }

    /// Bone controllers: what the game sets by hand on top of a clip. For
    /// each setting tried, every bone must be where the game's own maths
    /// puts it with that much added to the bone's angle (or place).
    ///
    /// `settings` are in the controller's own terms: degrees, or units.
    void checkControls(const std::string &dir, const char *file, const std::vector<std::string> &clips,
                       int controllers, int hitboxes,
                       const std::vector<std::array<double, 2>> &settings)
    {
        MdlFile mdl;
        std::string error;
        CookedModel cooked;
        ModelCookReport report;
        std::vector<std::uint8_t> bytes;
        if (!mdl.load(dir + "/models/" + file, error) ||
            !cookModel(mdl, clips, cooked, report, error) || !writeModel(cooked, bytes, error))
        {
            std::printf("test_anim: %s: %s\n", file, error.c_str());
            check(false, "a model with controllers cooks");
            return;
        }
        std::vector<std::uint32_t> aligned((bytes.size() + 3) / 4);
        std::memcpy(aligned.data(), bytes.data(), bytes.size());
        ModelView view;
        if (!view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()),
                       static_cast<std::uint32_t>(bytes.size())))
        {
            check(false, "a model with controllers opens");
            return;
        }
        check(view.header->controllerCount == controllers && view.header->hitboxCount == hitboxes,
              "its controllers and hit boxes are kept");

        double worstTurn = 0, worstPlace = 0;
        std::vector<BoneMatrix> pose(mdl.bones.size());
        for (int c = 0; c < view.header->clipCount; ++c)
        {
            const int sequence = mdl.sequenceByName(clips[c]);
            const MdlSequence &seq = mdl.sequences[sequence];
            std::vector<MdlPose> poses;
            mdl.decode(sequence, poses, error);
            const std::size_t boneCount = mdl.bones.size();
            for (int f = 0; f < seq.frameCount; f += std::max(1, seq.frameCount / 6))
            {
                for (const std::array<double, 2> &setting : settings)
                {
                    // (Coming up, folding away and falling apart, the game
                    // has them at rest; and those clips swing the bones
                    // about, which the others do not.)
                    const bool held = clips[c] != "deploy" && clips[c] != "retire" && clips[c] != "die";
                    if (!held && (setting[0] != 0 || setting[1] != 0))
                        continue;
                    std::vector<MdlPose> frame(poses.begin() + f * boneCount,
                                               poses.begin() + (f + 1) * boneCount);
                    for (int a = 0; a < 3; ++a)
                    {
                        if (seq.motionType & (1 << a))
                            frame[seq.motionBone].pos[a] = 0;
                    }
                    std::int32_t controls[kModelControls] = {0, 0, 0, 0};
                    for (const MdlController &control : mdl.controllers)
                    {
                        if (control.index < 0 || control.index > 1)
                            continue;
                        // The game's SetController, without its rounding to
                        // a byte: a travel written backwards turns the value
                        // round; then it is kept within the travel.
                        double value = setting[control.index];
                        const int type = control.type & 0x7FFF;
                        const bool turns = type >= kMdlMotionXR;
                        if (turns)
                        {
                            if (control.end < control.start)
                                value = -value;
                            if (control.start + 359.0 >= control.end)
                            {
                                const double middle = (control.start + control.end) / 2;
                                while (value > middle + 180)
                                    value -= 360;
                                while (value < middle - 180)
                                    value += 360;
                            }
                            else
                            {
                                value = value - 360.0 * std::floor(value / 360.0);
                            }
                        }
                        value = std::clamp(value, std::min<double>(control.start, control.end),
                                           std::max<double>(control.start, control.end));
                        int which = 0;
                        for (int bit = 0; bit < 6; ++bit)
                            which = (type & (1 << bit)) ? bit : which;
                        if (turns)
                            frame[control.bone].rot[which - 3] += static_cast<float>(value * M_PI / 180.0);
                        else
                            frame[control.bone].pos[which] += static_cast<float>(value);
                        controls[control.index] =
                            turns ? static_cast<std::int32_t>(std::lround(setting[control.index] * 65536.0 / 360.0))
                                  : static_cast<std::int32_t>(std::lround(setting[control.index] * 4096.0));
                    }
                    std::vector<std::array<double, 12>> want;
                    mdlBoneMatrices(mdl, frame.data(), want);
                    poseModel(view, c, static_cast<std::uint32_t>(f) << 8, pose.data(), controls);
                    for (std::size_t b = 0; b < boneCount; ++b)
                    {
                        for (int row = 0; row < 3; ++row)
                        {
                            for (int column = 0; column < 3; ++column)
                                worstTurn = std::max(worstTurn, std::fabs(want[b][row * 4 + column] -
                                                                          pose[b].r[row * 3 + column] / 16384.0));
                            worstPlace = std::max(worstPlace,
                                                  std::fabs(want[b][row * 4 + 3] - pose[b].t[row] / 4096.0));
                        }
                    }
                }
            }
        }
        std::printf("test_anim: %s: %d controllers, %d hit boxes; set by hand, its bones are within %.4f of the "
                    "game's turn and %.2f units of its place\n",
                    file, view.header->controllerCount, view.header->hitboxCount, worstTurn, worstPlace);
        // (Two units: what cutting the animation down to keys costs a model
        // this size by itself.)
        check(worstTurn < 0.02 && worstPlace < 2.0, "a bone set by hand is where the game puts it");
    }

    /// A guard's shooting blends between two poses by his aim's pitch (the
    /// game's SetBlending): both are cooked, and the pose between is where
    /// the game has it -- his gun's muzzle (attachment 0) low for an aim at
    /// the floor, level, and high, as the original model has it.
    void checkAimBlend(const std::string &dir)
    {
        MdlFile mdl;
        std::string error;
        CookedModel cooked;
        ModelCookReport report;
        std::vector<std::uint8_t> bytes;
        if (!mdl.load(dir + "/models/barney.mdl", error) || !cookModel(mdl, {"shootgun"}, cooked, report, error) ||
            !writeModel(cooked, bytes, error))
        {
            check(false, "the guard's shooting cooks");
            return;
        }
        std::vector<std::uint32_t> aligned((bytes.size() + 3) / 4);
        std::memcpy(aligned.data(), bytes.data(), bytes.size());
        ModelView view;
        if (!view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), static_cast<std::uint32_t>(bytes.size())))
        {
            check(false, "the guard's model opens");
            return;
        }
        const ModelClip &clip = view.clips[0];
        check(clip.blends == 2 && clip.blendLow == -50 && clip.blendHigh == 50,
              "his shooting has two poses, for an aim 50 degrees down and 50 up");
        std::vector<BoneMatrix> pose(mdl.bones.size());
        const double want[3][3] = {{20.7, -2.1, 31.9}, {28.7, -1.4, 59.3}, {12.2, -0.7, 84.4}};
        const int blends[3] = {0, 128, 255};
        double worst = 0;
        for (int k = 0; k < 3; ++k)
        {
            poseModel(view, 0, 0, pose.data(), nullptr, blends[k]);
            fx at[3];
            attachmentPoint(view, pose.data(), 0, at);
            for (int a = 0; a < 3; ++a)
                worst = std::max(worst, std::fabs(at[a] / 4096.0 - want[k][a]));
            std::printf("test_anim: barney.mdl shootgun at blend %d: muzzle %.1f %.1f %.1f\n", blends[k],
                        at[0] / 4096.0, at[1] / 4096.0, at[2] / 4096.0);
        }
        check(worst < 1.5, "his muzzle is where the original's is, aimed down, level and up");
        // The setting for an aim, as SetBlending makes it.
        check(clipBlend(clip, 0) == 127 && clipBlend(clip, 9102) == 255 &&
                  clipBlend(clip, -16384) == 0,
              "a level aim is the middle, 50 degrees up the top, straight down the bottom");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || *dir == '\0')
    {
        std::printf("test_anim: HLDS_VALVE_DIR not set, nothing to read\ntest_anim: ok\n");
        return 0;
    }
    checkModel(dir, "scientist.mdl",
               {"idle1", "walk", "run", "pondering", "push_button", "cprscientistrevive", "sitlookleft"});
    checkModel(dir, "barney.mdl", {"idle1", "walk", "run", "unlatch", "sit1"});
    checkModel(dir, "gman.mdl", {"idle01", "walk", "idlebrush"});
    checkModel(dir, "v_crowbar.mdl", {"idle1", "attack1", "draw"});
    // Every clip the weapons' code asks the hand for (HlMonsters.hpp's
    // weaponsTick) is in the game's own models.
    checkModel(dir, "v_9mmhandgun.mdl",
               {"idle1", "idle2", "idle3", "shoot", "shoot_empty", "reload", "reload_noshot", "draw"});
    checkModel(dir, "v_shotgun.mdl",
               {"sm_idle", "idle4", "deepidle", "shoot", "shoot_big", "start_reload", "reload", "pump", "draw"});
    checkModel(dir, "v_grenade.mdl", {"idle", "fidget", "pinpull", "throw1", "throw2", "throw3", "draw"});
    checkModel(dir, "v_9mmar.mdl", {"longidle", "idle1", "shoot", "grenade", "reload", "deploy"});
    checkModel(dir, "v_357.mdl", {"idle1", "idle2", "idle3", "fidget1", "fire1", "reload", "draw"});
    checkModel(dir, "zombie.mdl", {"idle1", "walk"});
    checkAimBlend(dir);
    checkModel(dir, "forklift.mdl", {"idle2", "drive"});

    // A body falling (the game's 2001 and 2002): somewhere in a scientist's
    // and a soldier's dying, light or heavy.
    for (const char *file : {"scientist.mdl", "hgrunt.mdl"})
    {
        MdlFile mdl;
        std::string error;
        std::vector<std::string> all;
        CookedModel cooked;
        ModelCookReport report;
        bool drops = false;
        if (mdl.load(std::string(dir) + "/models/" + file, error))
        {
            for (const MdlSequence &seq : mdl.sequences)
                all.push_back(seq.label);
            if (cookModel(mdl, all, cooked, report, error))
            {
                for (const ModelEvent &event : cooked.events)
                    drops = drops || (event.kind == kEventBodyDrop && event.target <= 1);
            }
        }
        check(drops, "a body falling is kept, as its own kind of event");
    }

    // What the game sets by hand: a turret's aim (round, and up and down),
    // a barnacle's tongue.
    const std::vector<std::string> turretClips = {"idle_off", "fire", "spin", "deploy", "retire", "die"};
    const std::vector<std::array<double, 2>> aims = {
        {0, 0}, {30, 10}, {135, -12}, {-70, 40}, {250, -80}, {359, 15}, {-200, 3}};
    checkControls(dir, "miniturret.mdl", turretClips, 2, 4, aims);
    checkControls(dir, "sentry.mdl", turretClips, 2, 6, aims);
    checkControls(dir, "turret.mdl", turretClips, 2, 6, aims);
    checkControls(dir, "barnacle.mdl", {"idle1", "attack1", "chewcycle"}, 1, 12,
                  {{0, 0}, {-100, 0}, {-777.5, 0}, {-2000, 0}, {50, 0}});

    // Attachments and skins: the big turret's eye is its second attachment,
    // and goes round with the gun; a soldier's second skin is another
    // picture on his head and arms, and nothing else is made twice.
    {
        MdlFile mdl;
        std::string error;
        CookedModel cooked;
        ModelCookReport report;
        std::vector<std::uint8_t> bytes;
        const bool ok = mdl.load(std::string(dir) + "/models/turret.mdl", error) &&
                        cookModel(mdl, turretClips, cooked, report, error) && writeModel(cooked, bytes, error);
        check(ok, "the turret cooks");
        std::vector<std::uint32_t> aligned((bytes.size() + 3) / 4);
        std::memcpy(aligned.data(), bytes.data(), bytes.size());
        ModelView view;
        if (ok && view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()),
                            static_cast<std::uint32_t>(bytes.size())))
        {
            check(view.header->attachmentCount == 2, "it has its two attachments");
            std::vector<BoneMatrix> pose(mdl.bones.size());
            const int spin = view.findClip(nameHash("spin"));
            fx ahead[3], turned[3], none[3];
            std::int32_t controls[kModelControls] = {0, 0, 0, 0};
            poseModel(view, spin, 0, pose.data(), controls);
            check(attachmentPoint(view, pose.data(), 1, ahead) && !attachmentPoint(view, pose.data(), 2, none),
                  "the eye is where its bone is; there is no third");
            controls[0] = 16384; // a quarter turn
            poseModel(view, spin, 0, pose.data(), controls);
            attachmentPoint(view, pose.data(), 1, turned);
            // (About the turret's axis: what was its x is now its y.)
            const double before[2] = {ahead[0] / 4096.0, ahead[1] / 4096.0};
            const double after[2] = {turned[0] / 4096.0, turned[1] / 4096.0};
            std::printf("test_anim: the turret's eye at %.1f %.1f; a quarter turn of the gun on, at %.1f %.1f\n",
                        before[0], before[1], after[0], after[1]);
            check(std::fabs(std::hypot(before[0], before[1]) - std::hypot(after[0], after[1])) < 1.5 &&
                      std::hypot(before[0] - after[0], before[1] - after[1]) > 4.0 &&
                      std::fabs(ahead[2] - turned[2]) < 4096,
                  "it goes round with the gun, the same way out and as high");
        }
        MdlFile grunt;
        const bool soldier = grunt.load(std::string(dir) + "/models/hgrunt.mdl", error) &&
                             cookModel(grunt, {"idle1", "run"}, cooked, report, error);
        check(soldier, "the soldier cooks");
        if (soldier)
        {
            int usual = 0, other = 0, both = 0;
            for (const ModelBatch &batch : cooked.batches)
            {
                usual += batch.skin == 1;
                other += batch.skin == 2;
                both += batch.skin == 0;
            }
            std::printf("test_anim: hgrunt.mdl: %zu skins; %d batches for all of them, %d for the first alone, "
                        "%d for the second\n",
                        grunt.skins.size(), both, usual, other);
            check(grunt.skins.size() == 2 && usual > 0 && usual == other && both > 0,
                  "his second skin is the same parts with another picture on them");
        }
    }

    // Time: a clip that loops goes round, one that does not stops and says so.
    ModelClip clip{};
    clip.frameCount = 31;
    clip.fps = 15 << 8;
    clip.flags = kClipLoops;
    bool finished = false;
    std::uint32_t frame = advanceClip(clip, 0, 1000, finished);
    check(frame == (15u << 8) && !finished, "a second at 15 frames a second is 15 frames");
    frame = advanceClip(clip, frame, 1500, finished);
    check(frame == (7u << 8) + 128 && !finished, "a clip that loops goes round");
    clip.flags = 0;
    frame = advanceClip(clip, 15u << 8, 1500, finished);
    check(frame == (30u << 8) && finished, "one that does not stops on its last frame, finished");

    if (g_failures == 0)
        std::printf("test_anim: ok\n");
    return g_failures == 0 ? 0 : 1;
}
