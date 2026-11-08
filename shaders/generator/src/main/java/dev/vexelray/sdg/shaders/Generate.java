package dev.vexelray.sdg.shaders;

import dev.supirvast.vastir.tools.NativeTools;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Lowers every SDG shader to SPIR-V, has spirv-val check it, and cross-compiles it by SPIRV-Cross to GLSL (450, and
 * 300 es), HLSL and Metal, into the folder named: {@code <name>.spv}, {@code .glsl}, {@code .es.glsl}, {@code .hlsl},
 * {@code .metal}. A shader spirv-val refuses stops the run, and nothing is written for it.
 */
public final class Generate {

    private Generate() {
    }

    public static void main(String[] args) throws IOException {
        Path out = Path.of(args.length > 0 ? args[0] : "out").toAbsolutePath().normalize();
        Files.createDirectories(out);
        NativeTools tools = new NativeTools();
        if (!tools.isAvailable()) {
            throw new IllegalStateException("spirv-val and SPIRV-Cross are not available on this platform");
        }
        Map<String, byte[]> shaders = new LinkedHashMap<>();
        shaders.put("sdg_layer.vert", SdgShaders.quadVertex(SdgShaders.LAYER, true));
        shaders.put("sdg_layer.frag", SdgShaders.layerFragment());
        shaders.put("sdg_group.vert", SdgShaders.quadVertex(SdgShaders.GROUP, true));
        shaders.put("sdg_group.frag", SdgShaders.groupFragment());
        for (var e : shaders.entrySet()) {
            NativeTools.ValidationResult v = tools.validate(e.getValue());
            if (!v.valid()) {
                throw new IllegalStateException("spirv-val refused " + e.getKey() + ":\n" + v.output());
            }
            String name = e.getKey();
            Files.write(out.resolve(name + ".spv"), e.getValue());
            write(out, name + ".glsl", tools.crossCompile(e.getValue(), NativeTools.ShaderLanguage.GLSL));
            write(out, name + ".es.glsl", tools.crossCompile(e.getValue(), NativeTools.ShaderLanguage.GLSL, 300));
            write(out, name + ".hlsl", tools.crossCompile(e.getValue(), NativeTools.ShaderLanguage.HLSL));
            write(out, name + ".metal", tools.crossCompile(e.getValue(), NativeTools.ShaderLanguage.MSL));
            System.out.println(name + ": valid SPIR-V, " + e.getValue().length + " bytes; GLSL, ESSL, HLSL, MSL");
        }
    }

    private static void write(Path dir, String name, String source) throws IOException {
        Files.writeString(dir.resolve(name), source, StandardCharsets.UTF_8);
    }
}
