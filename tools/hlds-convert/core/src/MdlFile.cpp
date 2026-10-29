#include "hlds/MdlFile.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "hlds/AtomicWrite.hpp"

namespace hlds
{
    namespace
    {
        // Offsets into studiohdr_t and friends (engine/studio.h), all
        // little-endian, ints and floats four bytes.
        constexpr std::size_t kHeaderSize = 244;
        constexpr std::size_t kBoneSize = 112;
        constexpr std::size_t kSeqSize = 176;
        constexpr std::size_t kSeqGroupSize = 104;
        constexpr std::size_t kEventSize = 76;
        constexpr std::size_t kBodyPartSize = 76;
        constexpr std::size_t kModelSize = 112;
        constexpr std::size_t kMeshSize = 20;
        constexpr std::size_t kTextureSize = 80;

        /// Bounds-checked reads from one file's bytes.
        struct Reader
        {
            const std::vector<std::uint8_t> &bytes;
            bool ok = true;

            bool has(std::size_t at, std::size_t size)
            {
                if (at > bytes.size() || size > bytes.size() - at)
                    ok = false;
                return ok;
            }
            std::int32_t i32(std::size_t at)
            {
                if (!has(at, 4))
                    return 0;
                std::int32_t v;
                std::memcpy(&v, &bytes[at], 4);
                return v;
            }
            std::int16_t i16(std::size_t at)
            {
                if (!has(at, 2))
                    return 0;
                std::int16_t v;
                std::memcpy(&v, &bytes[at], 2);
                return v;
            }
            std::uint16_t u16(std::size_t at) { return static_cast<std::uint16_t>(i16(at)); }
            float f32(std::size_t at)
            {
                if (!has(at, 4))
                    return 0;
                float v;
                std::memcpy(&v, &bytes[at], 4);
                return v;
            }
            std::string text(std::size_t at, std::size_t size)
            {
                if (!has(at, size))
                    return {};
                std::size_t length = 0;
                while (length < size && bytes[at + length] != 0)
                    ++length;
                return std::string(reinterpret_cast<const char *>(&bytes[at]), length);
            }
        };

        std::string lower(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        /// A file in `dir` whose name is `name` in any mix of cases.
        std::filesystem::path findNoCase(const std::filesystem::path &dir, const std::string &name)
        {
            std::error_code ec;
            const std::string want = lower(name);
            for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (lower(entry.path().filename().string()) == want)
                    return entry.path();
            }
            return {};
        }

        bool isStudio(const std::vector<std::uint8_t> &bytes, const char *id)
        {
            return bytes.size() >= 8 && std::memcmp(bytes.data(), id, 4) == 0 && bytes[4] == 10 &&
                   bytes[5] == 0 && bytes[6] == 0 && bytes[7] == 0;
        }

        /// The textures and skin families of an "IDST" file.
        bool readTextures(const std::vector<std::uint8_t> &bytes, std::vector<MdlTexture> &textures,
                          std::vector<std::vector<int>> &skins)
        {
            Reader r{bytes};
            const int count = r.i32(180);
            const std::size_t index = static_cast<std::uint32_t>(r.i32(184));
            if (count < 0 || count > 256)
                return false;
            for (int i = 0; i < count && r.ok; ++i)
            {
                const std::size_t at = index + static_cast<std::size_t>(i) * kTextureSize;
                MdlTexture texture;
                texture.name = lower(r.text(at, 64));
                texture.flags = r.i32(at + 64);
                texture.width = r.i32(at + 68);
                texture.height = r.i32(at + 72);
                const std::size_t pixels = static_cast<std::uint32_t>(r.i32(at + 76));
                if (texture.width <= 0 || texture.height <= 0 || texture.width > 1024 ||
                    texture.height > 1024)
                    return false;
                const std::size_t size = static_cast<std::size_t>(texture.width) * texture.height;
                if (!r.has(pixels, size + 768))
                    return false;
                texture.pixels.assign(bytes.begin() + pixels, bytes.begin() + pixels + size);
                std::memcpy(texture.palette.data(), &bytes[pixels + size], 768);
                textures.push_back(std::move(texture));
            }
            const int refs = r.i32(192);
            const int families = r.i32(196);
            const std::size_t skinIndex = static_cast<std::uint32_t>(r.i32(200));
            if (refs < 0 || refs > 256 || families < 0 || families > 64)
                return false;
            for (int f = 0; f < families && r.ok; ++f)
            {
                std::vector<int> family;
                for (int k = 0; k < refs; ++k)
                    family.push_back(r.i16(skinIndex + (static_cast<std::size_t>(f) * refs + k) * 2));
                skins.push_back(std::move(family));
            }
            return r.ok;
        }
    } // namespace

    int MdlSubModel::triangleCount() const
    {
        int count = 0;
        for (const MdlMesh &mesh : meshes)
        {
            for (const MdlStrip &strip : mesh.strips)
                count += std::max(0, static_cast<int>(strip.corners.size()) - 2);
        }
        return count;
    }

    bool MdlFile::load(const std::filesystem::path &path, std::string &error)
    {
        *this = MdlFile();
        name = lower(path.stem().string());
        if (!readFile(path, main_, error))
            return false;
        if (main_.size() < kHeaderSize || !isStudio(main_, "IDST"))
        {
            error = path.filename().string() + " is not a version 10 studio model";
            return false;
        }
        Reader r{main_};
        auto fail = [&](const char *what)
        {
            error = path.filename().string() + ": " + what;
            return false;
        };

        // ---- bones
        const int boneCount = r.i32(140);
        const std::size_t boneIndex = static_cast<std::uint32_t>(r.i32(144));
        if (boneCount <= 0 || boneCount > 128)
            return fail("an impossible number of bones");
        for (int i = 0; i < boneCount; ++i)
        {
            const std::size_t at = boneIndex + static_cast<std::size_t>(i) * kBoneSize;
            MdlBone bone;
            bone.name = r.text(at, 32);
            bone.parent = r.i32(at + 32);
            for (int c = 0; c < 6; ++c)
            {
                bone.value[c] = r.f32(at + 64 + c * 4);
                bone.scale[c] = r.f32(at + 88 + c * 4);
                bone.controller[c] = r.i32(at + 40 + c * 4);
            }
            // The cooker and the DS both build a pose parents first, in
            // order: a parent must come before its children.
            if (bone.parent >= i || bone.parent < -1)
                return fail("a bone comes before its parent");
            bones.push_back(std::move(bone));
        }

        // ---- bone controllers and hit boxes
        const int controllerCount = r.i32(148);
        const std::size_t controllerIndex = static_cast<std::uint32_t>(r.i32(152));
        if (controllerCount < 0 || controllerCount > 8)
            return fail("an impossible number of bone controllers");
        for (int i = 0; i < controllerCount; ++i)
        {
            const std::size_t at = controllerIndex + static_cast<std::size_t>(i) * 24;
            MdlController c;
            c.bone = r.i32(at);
            c.type = r.i32(at + 4);
            c.start = r.f32(at + 8);
            c.end = r.f32(at + 12);
            c.index = r.i32(at + 20);
            if (c.bone < 0 || c.bone >= boneCount)
                return fail("a bone controller on a bone there is not");
            controllers.push_back(c);
        }
        for (MdlBone &bone : bones)
        {
            for (int &c : bone.controller)
                c = c >= 0 && c < controllerCount ? c : -1;
        }
        const int hitboxCount = r.i32(156);
        const std::size_t hitboxIndex = static_cast<std::uint32_t>(r.i32(160));
        if (hitboxCount < 0 || hitboxCount > 128)
            return fail("an impossible number of hit boxes");
        for (int i = 0; i < hitboxCount; ++i)
        {
            const std::size_t at = hitboxIndex + static_cast<std::size_t>(i) * 32;
            MdlHitbox box;
            box.bone = r.i32(at);
            box.group = r.i32(at + 4);
            for (int a = 0; a < 3; ++a)
            {
                box.mins[a] = r.f32(at + 8 + a * 4);
                box.maxs[a] = r.f32(at + 20 + a * 4);
            }
            if (box.bone < 0 || box.bone >= boneCount)
                return fail("a hit box on a bone there is not");
            hitboxes.push_back(box);
        }

        // ---- attachments
        const int attachmentCount = r.i32(212);
        const std::size_t attachmentIndex = static_cast<std::uint32_t>(r.i32(216));
        if (attachmentCount < 0 || attachmentCount > 8)
            return fail("an impossible number of attachments");
        for (int i = 0; i < attachmentCount; ++i)
        {
            const std::size_t at = attachmentIndex + static_cast<std::size_t>(i) * 88;
            Attachment a;
            a.bone = r.i32(at + 36);
            for (int k = 0; k < 3; ++k)
                a.at[k] = r.f32(at + 40 + k * 4);
            if (a.bone < 0 || a.bone >= boneCount)
                return fail("an attachment on a bone there is not");
            attachments.push_back(a);
        }

        // ---- sequence groups: the files the animation of some sequences is in
        const int groupCount = r.i32(172);
        if (groupCount < 1 || groupCount > 64)
            return fail("an impossible number of sequence groups");
        groups_.resize(groupCount);
        for (int g = 1; g < groupCount; ++g)
        {
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), "%02d.mdl", g);
            const std::filesystem::path file =
                findNoCase(path.parent_path(), path.stem().string() + suffix);
            std::string ignored;
            if (file.empty() || !readFile(file, groups_[g], ignored) || !isStudio(groups_[g], "IDSQ"))
                return fail("one of its sequence files is missing");
        }

        // ---- sequences
        const int seqCount = r.i32(164);
        const std::size_t seqIndex = static_cast<std::uint32_t>(r.i32(168));
        if (seqCount < 0 || seqCount > 2048)
            return fail("an impossible number of sequences");
        for (int i = 0; i < seqCount; ++i)
        {
            const std::size_t at = seqIndex + static_cast<std::size_t>(i) * kSeqSize;
            MdlSequence seq;
            seq.label = r.text(at, 32);
            seq.fps = r.f32(at + 32);
            seq.flags = r.i32(at + 36);
            seq.activity = r.i32(at + 40);
            seq.actWeight = r.i32(at + 44);
            const int eventCount = r.i32(at + 48);
            const std::size_t eventIndex = static_cast<std::uint32_t>(r.i32(at + 52));
            seq.frameCount = r.i32(at + 56);
            seq.motionType = r.i32(at + 68);
            seq.motionBone = r.i32(at + 72);
            for (int a = 0; a < 3; ++a)
                seq.linearMovement[a] = r.f32(at + 76 + a * 4);
            seq.blendCount = r.i32(at + 120);
            seq.animOffset = static_cast<std::uint32_t>(r.i32(at + 124));
            seq.blendType = r.i32(at + 128);
            seq.blendStart = r.f32(at + 136);
            seq.blendEnd = r.f32(at + 144);
            seq.group = r.i32(at + 156);
            if (seq.frameCount < 1 || seq.frameCount > 4096 || seq.group < 0 ||
                seq.group >= groupCount || eventCount < 0 || eventCount > 256)
                return fail("a sequence that makes no sense");
            for (int e = 0; e < eventCount; ++e)
            {
                const std::size_t ev = eventIndex + static_cast<std::size_t>(e) * kEventSize;
                MdlEvent event;
                event.frame = r.i32(ev);
                event.event = r.i32(ev + 4);
                event.options = r.text(ev + 12, 64);
                seq.events.push_back(std::move(event));
            }
            sequences.push_back(std::move(seq));
        }

        // ---- body parts, their sub-models and meshes
        const int partCount = r.i32(204);
        const std::size_t partIndex = static_cast<std::uint32_t>(r.i32(208));
        if (partCount < 0 || partCount > 32)
            return fail("an impossible number of body parts");
        for (int p = 0; p < partCount; ++p)
        {
            const std::size_t at = partIndex + static_cast<std::size_t>(p) * kBodyPartSize;
            MdlBodyPart part;
            part.name = r.text(at, 64);
            const int modelCount = r.i32(at + 64);
            part.base = std::max(1, r.i32(at + 68));
            const std::size_t modelIndex = static_cast<std::uint32_t>(r.i32(at + 72));
            if (modelCount < 0 || modelCount > 64)
                return fail("an impossible number of sub-models");
            for (int m = 0; m < modelCount; ++m)
            {
                const std::size_t mo = modelIndex + static_cast<std::size_t>(m) * kModelSize;
                MdlSubModel model;
                model.name = r.text(mo, 64);
                const int meshCount = r.i32(mo + 72);
                const std::size_t meshIndex = static_cast<std::uint32_t>(r.i32(mo + 76));
                const int vertCount = r.i32(mo + 80);
                const std::size_t vertInfo = static_cast<std::uint32_t>(r.i32(mo + 84));
                const std::size_t vertIndex = static_cast<std::uint32_t>(r.i32(mo + 88));
                const int normCount = r.i32(mo + 92);
                const std::size_t normInfo = static_cast<std::uint32_t>(r.i32(mo + 96));
                const std::size_t normIndex = static_cast<std::uint32_t>(r.i32(mo + 100));
                if (meshCount < 0 || meshCount > 256 || vertCount < 0 || vertCount > 8192 ||
                    normCount < 0 || normCount > 8192 || !r.has(vertInfo, vertCount) ||
                    !r.has(vertIndex, static_cast<std::size_t>(vertCount) * 12) ||
                    !r.has(normInfo, normCount) ||
                    !r.has(normIndex, static_cast<std::size_t>(normCount) * 12))
                    return fail("a sub-model that runs off the end of the file");
                for (int v = 0; v < vertCount; ++v)
                {
                    model.vertices.push_back({r.f32(vertIndex + v * 12), r.f32(vertIndex + v * 12 + 4),
                                              r.f32(vertIndex + v * 12 + 8)});
                    model.vertexBone.push_back(main_[vertInfo + v]);
                    if (model.vertexBone.back() >= boneCount)
                        return fail("a vertex on a bone that is not there");
                }
                for (int n = 0; n < normCount; ++n)
                {
                    model.normals.push_back({r.f32(normIndex + n * 12), r.f32(normIndex + n * 12 + 4),
                                             r.f32(normIndex + n * 12 + 8)});
                    model.normalBone.push_back(main_[normInfo + n]);
                }
                for (int k = 0; k < meshCount; ++k)
                {
                    const std::size_t me = meshIndex + static_cast<std::size_t>(k) * kMeshSize;
                    MdlMesh mesh;
                    mesh.triangles = r.i32(me);
                    mesh.skinRef = r.i32(me + 8);
                    // The triangles, as the game draws them: a count (a strip
                    // if positive, a fan if negative, the end if zero), then
                    // that many corners of four shorts each.
                    std::size_t cursor = static_cast<std::uint32_t>(r.i32(me + 4));
                    for (int guard = 0; guard < 65536; ++guard)
                    {
                        const int count = r.i16(cursor);
                        cursor += 2;
                        if (!r.ok || count == 0)
                            break;
                        MdlStrip strip;
                        strip.fan = count < 0;
                        const int corners = count < 0 ? -count : count;
                        if (!r.has(cursor, static_cast<std::size_t>(corners) * 8))
                            return fail("a mesh that runs off the end of the file");
                        for (int c = 0; c < corners; ++c, cursor += 8)
                        {
                            MdlCorner corner;
                            corner.vertex = r.i16(cursor);
                            corner.normal = r.i16(cursor + 2);
                            corner.s = r.i16(cursor + 4);
                            corner.t = r.i16(cursor + 6);
                            if (corner.vertex < 0 || corner.vertex >= vertCount ||
                                corner.normal < 0 || corner.normal >= normCount)
                                return fail("a triangle with a corner that is not there");
                            strip.corners.push_back(corner);
                        }
                        mesh.strips.push_back(std::move(strip));
                    }
                    model.meshes.push_back(std::move(mesh));
                }
                part.models.push_back(std::move(model));
            }
            bodyParts.push_back(std::move(part));
        }
        if (!r.ok)
            return fail("the file is cut short");

        // ---- textures: here, or in the file that goes with this one
        if (r.i32(180) > 0)
        {
            if (!readTextures(main_, textures, skins))
                return fail("its textures cannot be read");
        }
        else
        {
            const std::filesystem::path file =
                findNoCase(path.parent_path(), path.stem().string() + "t.mdl");
            std::vector<std::uint8_t> bytes;
            std::string ignored;
            if (file.empty() || !readFile(file, bytes, ignored) || bytes.size() < kHeaderSize ||
                !isStudio(bytes, "IDST") || !readTextures(bytes, textures, skins))
                return fail("its texture file is missing or cannot be read");
        }
        for (const std::vector<int> &family : skins)
        {
            for (int texture : family)
            {
                if (texture < 0 || texture >= static_cast<int>(textures.size()))
                    return fail("a skin that names a texture that is not there");
            }
        }
        return true;
    }

    bool MdlFile::decode(int sequence, std::vector<MdlPose> &out, std::string &error, int blend) const
    {
        out.clear();
        if (sequence < 0 || sequence >= static_cast<int>(sequences.size()))
        {
            error = "no such sequence";
            return false;
        }
        const MdlSequence &seq = sequences[sequence];
        if (blend < 0 || blend >= std::max(1, seq.blendCount))
        {
            error = "no such blend";
            return false;
        }
        const std::vector<std::uint8_t> &bytes = seq.group == 0 ? main_ : groups_[seq.group];
        Reader r{bytes};
        const std::size_t boneCount = bones.size();
        // (Each blend's six offsets a bone follow the one before's.)
        const std::size_t blendOffset = static_cast<std::size_t>(blend) * boneCount * 12;
        out.resize(static_cast<std::size_t>(seq.frameCount) * boneCount);

        for (std::size_t b = 0; b < boneCount; ++b)
        {
            // Six offsets per bone, each from the start of this bone's six;
            // 0 for a channel the sequence does not animate.
            const std::size_t anim = seq.animOffset + blendOffset + b * 12;
            for (int channel = 0; channel < 6; ++channel)
            {
                const std::uint16_t offset = r.u16(anim + channel * 2);
                for (int frame = 0; frame < seq.frameCount; ++frame)
                {
                    float value = bones[b].value[channel];
                    if (offset != 0)
                    {
                        // Runs of {valid, total} followed by `valid` values:
                        // the run covers `total` frames, the last value
                        // holding for those past `valid`.
                        std::size_t at = anim + offset;
                        int k = frame;
                        for (int guard = 0; guard < 8192; ++guard)
                        {
                            if (!r.has(at, 2))
                                break;
                            const int valid = bytes[at];
                            const int total = bytes[at + 1];
                            if (total == 0)
                            {
                                r.ok = false;
                                break;
                            }
                            if (total > k)
                            {
                                const int pick = valid > k ? k + 1 : valid;
                                value += r.i16(at + static_cast<std::size_t>(pick) * 2) *
                                         bones[b].scale[channel];
                                break;
                            }
                            k -= total;
                            at += static_cast<std::size_t>(valid + 1) * 2;
                        }
                    }
                    MdlPose &pose = out[static_cast<std::size_t>(frame) * boneCount + b];
                    if (channel < 3)
                        pose.pos[channel] = value;
                    else
                        pose.rot[channel - 3] = value;
                }
            }
        }
        if (!r.ok)
        {
            error = "the animation of '" + seq.label + "' runs off the end of its file";
            out.clear();
            return false;
        }
        return true;
    }

    int MdlFile::sequenceByName(const std::string &label) const
    {
        const std::string want = lower(label);
        for (std::size_t i = 0; i < sequences.size(); ++i)
        {
            if (lower(sequences[i].label) == want)
                return static_cast<int>(i);
        }
        return -1;
    }

    std::vector<const MdlSubModel *> MdlFile::selectBody(int body) const
    {
        std::vector<const MdlSubModel *> chosen;
        for (const MdlBodyPart &part : bodyParts)
        {
            if (part.models.empty())
                continue;
            const int index = (std::max(0, body) / part.base) % static_cast<int>(part.models.size());
            chosen.push_back(&part.models[index]);
        }
        return chosen;
    }

    std::vector<char> MdlFile::usedBones() const
    {
        std::vector<char> used(bones.size(), 0);
        for (const MdlBodyPart &part : bodyParts)
        {
            for (const MdlSubModel &model : part.models)
            {
                for (std::uint8_t bone : model.vertexBone)
                    used[bone] = 1;
            }
        }
        return used;
    }
} // namespace hlds
