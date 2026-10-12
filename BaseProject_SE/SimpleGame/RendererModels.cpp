#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Renderer.h"
#include "Profiler.h"

#include <cmath>
#include <iomanip>
#include <utility>

namespace
{
    constexpr int ModelCacheVersion = 1;
}

bool Renderer::HasModel(const std::string& name) const
{
    return m_Models.find(name) != m_Models.end();
}

void Renderer::BeginModel(const std::string& name)
{
    m_CompiledModels.erase(name);
    m_RecordingModel = name;
    m_Models[name].clear();
    m_Material = 0;
}

void Renderer::EndModel()
{
    m_RecordingModel.clear();
    m_Material = 0;
}

bool Renderer::LoadModels(const std::wstring& path)
{
    Perf::Scope scope(Perf::Cpu::ModelLoad);
    std::ifstream file(path.c_str());
    std::string magic;
    int version = 0;
    size_t count = 0;
    if (!(file >> magic >> version >> count) || magic != "MIST_MODELS" || version != ModelCacheVersion ||
        count == 0 || count > 256)
    {
        return false;
    }

    std::map<std::string, std::vector<ModelPart>> loaded;
    size_t totalParts = 0;
    for (size_t i = 0; i < count; ++i)
    {
        std::string name;
        size_t parts = 0;
        if (!(file >> name >> parts) || name.size() > 64 || parts == 0 || parts > 2048 ||
            loaded.count(name) || (totalParts += parts) > 65536)
        {
            return false;
        }

        std::vector<ModelPart> model(parts);
        for (auto& part : model)
        {
            if (!(file >> part.shape >> part.material) || part.shape < 0 || part.shape > 2 ||
                part.material < 0 || part.material > 10)
            {
                return false;
            }
            for (float& value : part.values)
            {
                if (!(file >> value) || !std::isfinite(value) || std::abs(value) > 10000)
                {
                    return false;
                }
            }
            if (part.shape < 2 && (part.values[2] <= 0 || part.values[3] <= 0))
            {
                return false;
            }
        }
        loaded.emplace(name, std::move(model));
    }

    std::string extra;
    if (file >> extra)
    {
        return false;
    }
    m_Models = std::move(loaded);
    m_CompiledModels.clear();
    return true;
}

bool Renderer::SaveModels(const std::wstring& path) const
{
    Perf::Scope scope(Perf::Cpu::ModelSave);
    // Replace only this versioned cache after the complete file is flushed.
    std::wstring temporary = path + L".tmp";
    std::ofstream file(temporary.c_str(), std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << "MIST_MODELS " << ModelCacheVersion << ' ' << m_Models.size() << '\n';
    file << std::setprecision(9);
    for (const auto& model : m_Models)
    {
        file << model.first << ' ' << model.second.size() << '\n';
        for (const auto& part : model.second)
        {
            file << part.shape << ' ' << part.material;
            for (float value : part.values)
            {
                file << ' ' << value;
            }
            file << '\n';
        }
    }
    file.flush();
    if (!file.good())
    {
        return false;
    }
    file.close();
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
           FALSE;
}

void Renderer::DrawModel(const std::string& name, float x, float y)
{
    auto model = m_Models.find(name);
    if (model == m_Models.end())
    {
        return;
    }
    auto compiled = m_CompiledModels.find(name);
    if (compiled != m_CompiledModels.end())
    {
        Perf::Count(Perf::Counter::ModelCacheHit);
        for (auto item : compiled->second)
        {
            for (int i = 0; i < 8; i += 2)
            {
                item.corners[i] += x;
                item.corners[i + 1] += y;
            }
            QueueInstance(item);
        }
        return;
    }
    Perf::Scope scope(Perf::Cpu::ModelBuild);
    Perf::Count(Perf::Counter::ModelCacheMiss);
    float originX = x, originY = y;
    x = y = 0;
    m_ModelInstances.clear();
    m_CompileModel = true;
    int previousMaterial = m_Material;
    for (const auto& part : model->second)
    {
        const auto& v = part.values;
        m_Material = part.material;
        if (part.shape == 0)
        {
            DrawSolidRect(x + v[0], y + v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        }
        else if (part.shape == 1)
        {
            DrawSolidDiamond(x + v[0], y + v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        }
        else
        {
            DrawQuad(x + v[0],
                     y + v[1],
                     x + v[2],
                     y + v[3],
                     x + v[4],
                     y + v[5],
                     x + v[6],
                     y + v[7],
                     v[8],
                     v[9],
                     v[10]);
        }
    }
    m_Material = previousMaterial;
    m_CompileModel = false;
    m_CompiledModels[name] = std::move(m_ModelInstances);
    for (auto item : m_CompiledModels.at(name))
    {
        for (int i = 0; i < 8; i += 2)
        {
            item.corners[i] += originX;
            item.corners[i + 1] += originY;
        }
        QueueInstance(item);
    }
}
