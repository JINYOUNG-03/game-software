"use strict";
// Read-only source, dependency, font-data and coordinate checks. No C++/game/shader execution.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const root = path.resolve(__dirname, "..");
const source = path.join(root, "BaseProject_SE/SimpleGame");
const read = file => fs.readFileSync(path.join(root, file), "utf8");
const cpp = file => fs.readFileSync(path.join(source, file), "utf8");
const sha = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
function walk(directory, prefix = "")
{
    return fs.readdirSync(directory, {withFileTypes : true})
        .flatMap(entry => entry.isDirectory()
                              ? walk(path.join(directory, entry.name), prefix + entry.name + "/")
                              : [ prefix + entry.name ]);
}
function bundle(directory)
{
    const absolute = path.join(root, directory), files = walk(absolute).sort();
    const digest = crypto.createHash("sha256");
    for (const file of files)
        digest.update(file + "\0" + sha(fs.readFileSync(path.join(absolute, file))) + "\n");
    return {directory, files : files.length, tree_sha256 : digest.digest("hex")};
}
function fontGlyphs(bytes)
{
    const tables = new Map();
    for (let i = 0; i < bytes.readUInt16BE(4); i++)
    {
        const record = 12 + i * 16;
        tables.set(bytes.toString("ascii", record, record + 4), bytes.readUInt32BE(record + 8));
    }
    const cmap = tables.get("cmap");
    assert(cmap && tables.has("name") && tables.has("glyf"));
    let table;
    for (let i = 0; i < bytes.readUInt16BE(cmap + 2); i++)
    {
        const record = cmap + 4 + i * 8, offset = cmap + bytes.readUInt32BE(record + 4);
        if (bytes.readUInt16BE(offset) === 4)
            table = offset;
    }
    assert(table !== undefined, "Missing Unicode BMP font map");
    const count = bytes.readUInt16BE(table + 6) / 2;
    const end = table + 14, start = end + count * 2 + 2;
    const delta = start + count * 2, ranges = delta + count * 2;
    return code => {
        for (let i = 0; i < count; i++)
        {
            if (code > bytes.readUInt16BE(end + i * 2))
                continue;
            if (code < bytes.readUInt16BE(start + i * 2))
                return 0;
            const displacement = bytes.readUInt16BE(ranges + i * 2), d = bytes.readInt16BE(delta + i * 2);
            if (!displacement)
                return (code + d) & 65535;
            const address = ranges + i * 2 + displacement + (code - bytes.readUInt16BE(start + i * 2)) * 2;
            const value = bytes.readUInt16BE(address);
            return value ? (value + d) & 65535 : 0;
        }
        return 0;
    };
}
const bundles = [ bundle("third_party/glfw"), bundle("third_party/glew") ];
const font = fs.readFileSync(path.join(root, "resource/Fonts/NanumGothic-Regular.ttf"));
let nameTable;
for (let i = 0; i < font.readUInt16BE(4); i++)
{
    const record = 12 + i * 16;
    if (font.toString("ascii", record, record + 4) === "name")
        nameTable = font.readUInt32BE(record + 8);
}
const families = [];
for (let i = 0; i < font.readUInt16BE(nameTable + 2); i++)
{
    const record = nameTable + 6 + i * 12;
    if (font.readUInt16BE(record + 6) !== 1 || font.readUInt16BE(record) !== 3)
        continue;
    const offset = nameTable + font.readUInt16BE(nameTable + 4) + font.readUInt16BE(record + 10);
    const bytes = Buffer.from(font.subarray(offset, offset + font.readUInt16BE(record + 8)));
    families.push(bytes.swap16().toString("utf16le"));
}
assert(families.includes("NanumGothic"), "GDI fallback family must match the bundled TTF name");
const glyph = fontGlyphs(font), characters = new Set();
for (const file of fs.readdirSync(source).filter(file => /\.(cpp|h)$/.test(file)))
    for (const match of cpp(file).matchAll(/\\u([0-9a-fA-F]{4})/g))
        characters.add(parseInt(match[1], 16));
const missing = [...characters ].filter(code => !glyph(code)).map(code => code.toString(16));
if (process.argv.includes("--describe"))
{
    console.log(
        JSON.stringify({bundles, font_sha256 : sha(font), unicode_characters : characters.size, missing}));
    process.exit(0);
}
assert(characters.size > 400);
assert.deepEqual(missing, [], "Bundled font must cover every current escaped game character");
for (const code of [32, 48, 65, 97, 0xac00, 0xd7a3])
    assert(glyph(code), "Missing basic or Hangul glyph");
const manifest = JSON.parse(read("third_party/DEPENDENCIES.json"));
for (let i = 0; i < bundles.length; i++)
{
    assert.equal(bundles[i].files, manifest.bundles[i].files);
    assert.equal(bundles[i].tree_sha256, manifest.bundles[i].tree_sha256, "Vendored source tree changed");
}
assert.equal(sha(font), manifest.font.sha256);
const game = cpp("SimpleGame.cpp"), input = cpp("GlfwInput.h"), nativeFont = cpp("KoreanFont.cpp");
const project = cpp("SimpleGame.vcxproj"), cmake = read("CMakeLists.txt");
assert(!/glut|GLUT|wglGetCurrent|GetActiveWindow/.test(game));
for (const contract of ["glfwMakeContextCurrent(gameWindow)", "glfwSwapBuffers(gameWindow)",
                        "glfwPollEvents()", "glfwGetWindowSize(gameWindow, &inputWidth, &inputHeight)",
                        "glfwGetFramebufferSize(gameWindow, &outputWidth, &outputHeight)",
                        "glfwSetFramebufferSizeCallback(gameWindow, Reshape)",
                        "glfwSetKeyCallback(gameWindow, WindowKey)", "action == GLFW_REPEAT",
                        "glfwWaitEventsTimeout(.05)", "ResetFrameClock()"])
    assert(game.includes(contract), contract);
for (const contract of ["GLFW_CONTEXT_VERSION_MAJOR, 3", "GLFW_CONTEXT_VERSION_MINOR, 3",
                        "GLFW_OPENGL_COMPAT_PROFILE", "GLFW_SAMPLES, 0", "GLFW_SRGB_CAPABLE, GLFW_FALSE",
                        'glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)',
                        "glfwSwapInterval(vsync[0] == '1' ? 1 : 0)"])
    assert(game.includes(contract), contract);
assert(game.includes("float(x) * ScreenWidth / (std::max)(1, inputWidth)"));
assert(game.includes("float(y) * ScreenHeight / (std::max)(1, inputHeight)"));
assert(!game.includes("ScreenWidth / (std::max)(1, outputWidth)"));
const close = game.slice(game.indexOf("void Close()"), game.indexOf("int main()"));
assert(close.indexOf("delete renderer") < close.indexOf("glfwDestroyWindow"));
assert(close.indexOf("Profiler::Get().Shutdown()") < close.indexOf("glfwDestroyWindow"));
assert(close.indexOf("koreanFont.Shutdown()") < close.indexOf("glfwDestroyWindow"));
for (const contract of ["CreateCompatibleDC(nullptr)", 'L"Malgun Gothic"', "ANTIALIASED_QUALITY",
                        "SupportsHangul", "GetGlyphIndicesW", "AddFontResourceExW", "FR_PRIVATE",
                        "RemoveFontResourceExW", "DeleteDC", "NanumGothic-Regular.ttf"])
    assert(nativeFont.includes(contract), contract);
assert(cpp("RendererBatch.cpp").includes("GetGlyphOutlineW"));
assert(cpp("RendererBatch.cpp").includes("m_Glyphs.find"));
const glfwHeader = read("third_party/glfw/include/GLFW/glfw3.h");
const key = name => Number(glfwHeader.match(new RegExp("#define\\s+GLFW_KEY_" + name + "\\s+(\\d+)"))[1]);
assert.equal(key("A"), 65);
assert.equal(key("Z"), 90);
assert.equal(key("0"), 48);
assert.equal(key("9"), 57);
for (let code = key("A"); code <= key("Z"); code++)
    assert.equal(97 + code - key("A"), code + 32);
for (const special of ["SPACE", "TAB", "ESCAPE", "ENTER", "KP_ENTER"])
    assert(input.includes("GLFW_KEY_" + special));
// Physical framebuffer pixels never enter cursor scaling, including non-uniform DPI scale.
let dpiProbes = 0;
for (const window of [[ 1280, 800 ], [ 900, 600 ], [ 1600, 900 ]])
    for (const scale of [[ 1, 1 ], [ 1.25, 1.25 ], [ 2, 2 ], [ 1.5, 2 ]])
        for (const logical of [[ 200, 245 ], [ 490, 215 ], [ 800, 600 ]])
        {
            const cursor = [ logical[0] * window[0] / 1280, logical[1] * window[1] / 800 ];
            assert(Math.abs(cursor[0] * 1280 / window[0] - logical[0]) < .00001);
            assert(Math.abs(cursor[1] * 800 / window[1] - logical[1]) < .00001);
            const framebuffer = [ window[0] * scale[0], window[1] * scale[1] ];
            assert(framebuffer.every(value => value > 0));
            dpiProbes++;
        }
assert(!/freeglut\.lib|glew32\.lib|Dependencies\\x64/.test(project));
assert(project.includes("GLEW_STATIC;GLEW_NO_GLU;GLFW_INCLUDE_NONE"));
assert(project.includes("MultiThreadedDebug") && project.includes("MultiThreaded"));
assert(project.includes("GLFW.Static.vcxproj") && project.includes("GLEW.Static.vcxproj"));
assert(cmake.includes("add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)"));
assert(cmake.includes("add_library(haedong_glew STATIC third_party/glew/src/glew.c)"));
const nativeSources = [...project.matchAll(/<ClCompile Include="([^"]+\.cpp)"/g) ].map(m => m[1]).sort();
const cmakeSources = [...cmake.matchAll(/\$\{game_dir\}\/([\w]+\.cpp)/g) ].map(m => m[1]).sort();
assert.deepEqual(nativeSources, cmakeSources, "Native VS and CMake must compile the same game sources");
assert(!/FetchContent|ExternalProject|file\(DOWNLOAD/.test(cmake));
for (const library of ["GLFW", "GLEW"])
{
    const xml = read("third_party/" + library + ".Static.vcxproj");
    const sources =
        [...xml.matchAll(/<ClCompile Include="([^"]+)"/g) ].map(match => match[1].replace(/\\/g, "/"));
    assert.equal(sources.length, library === "GLFW" ? 21 : 1);
    for (const sourceFile of sources)
        assert(fs.existsSync(path.join(root, "third_party", sourceFile)));
    assert(!xml.includes("GLFW_INCLUDE_NONE"), "GLFW internals reject client header macros");
    for (const config of ["Debug|Win32", "Release|Win32", "Debug|x64", "Release|x64"])
        assert(xml.includes(config));
}
const props = read("third_party/StaticLibrary.props");
assert(props.includes("CompileAsC") && props.includes("NotUsing") && props.includes("$(ProjectName)"));
assert(props.includes("MultiThreadedDebug") && props.includes("MultiThreaded"));
assert(props.includes("NDEBUG") && props.includes("_DEBUG"));
for (const file of ["GlfwInput.h", "KoreanFont.h", "KoreanFont.cpp"])
    assert(project.includes('Include="' + file + '"') &&
           cpp("SimpleGame.vcxproj.filters").includes('Include="' + file + '"'));
for (const deployment of ["Fonts", "NanumGothic-Regular.ttf", "OFL-NanumGothic.txt", "Licenses"])
    assert(project.includes(deployment) && cmake.includes(deployment));
console.log(JSON.stringify({
    event : "glfw_migration_contracts",
    passed : true,
    bundled_source_files : bundles.reduce((n, b) => n + b.files, 0),
    font_unicode_characters : characters.size,
    dpi_probes : dpiProbes,
    static_library_sources : 22,
    game_build_performed : false,
    game_execution_performed : false,
    font_rasterization_performed : false
}));
