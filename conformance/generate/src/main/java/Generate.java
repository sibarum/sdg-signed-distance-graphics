import dev.vexelray.fields.art.Raster;
import dev.vexelray.fields.art.Svg;
import dev.vexelray.fields.sdg.Png;
import dev.vexelray.fields.sdg.Sdg;
import dev.vexelray.fields.sdg.SdgFile;
import dev.vexelray.fields.sdg.SdgRender;
import dev.vexelray.fields.sdg.SvgToSdg;
import dev.vexelray.fields.sdg.Trace;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The conformance set: each piece of art as a compiled SDG ({@code drawings/}), and what the reference draws of it at
 * each view ({@code expected/}), listed in {@code cases.txt}. The reference is vexelray-fields' {@code SdgRender}, which
 * the spec (vexelray-fields docs/sdg.md) names as such.
 *
 * <p>Three views a drawing: fitted into 64 x 64; fitted into 200 x 150, not square; and a corner magnified eight times,
 * where filtering between texels shows. A traced picture adds sampled paints, which the SVGs here have only as gradients.
 */
public final class Generate {

    static final String[] ART = {"badge", "rasterware", "vexelray-logo", "mainframe", "background-icon", "file-icon",
            "group-opacity", "group-clip", "group-mask", "group-nested"};

    public static void main(String[] args) throws IOException {
        Path dir = Path.of(args.length > 0 ? args[0] : "..").toAbsolutePath().normalize();
        Files.createDirectories(dir.resolve("drawings"));
        Files.createDirectories(dir.resolve("expected"));
        List<String> cases = new ArrayList<>();
        cases.add("# drawing width height originX originY unitsPerPixel expected");
        cases.add("# Written by conformance/generate (Generate.java) from vexelray-fields' SdgRender, the reference.");
        for (String name : ART) {
            Svg svg = Svg.parse(read("/art/" + name + ".svg"));
            write(dir, name, SvgToSdg.convert(svg, SvgToSdg.Options.across(svg, 64)), cases);
        }
        // a traced picture: colour regions, and sampled paints where a region's colour varies
        Svg badge = Svg.parse(read("/art/badge.svg"));
        Raster picture = badge.render(96, 96, 4);
        write(dir, "traced-badge", Trace.trace(picture, Trace.Options.DEFAULT), cases);
        Files.write(dir.resolve("cases.txt"), cases, StandardCharsets.UTF_8);
        System.out.println("wrote " + (cases.size() - 2) + " cases to " + dir);
    }

    private static void write(Path dir, String name, Sdg sdg, List<String> cases) throws IOException {
        String file = "drawings/" + name + ".sdgb";
        SdgFile.write(sdg, dir.resolve(file));
        view(dir, name, sdg, cases, file, "fit64", 64, 64);
        view(dir, name, sdg, cases, file, "fit200x150", 200, 150);
        // a corner, eight times what fitting it into 64 would show
        double upp = Math.max(sdg.width(), sdg.height()) / 64 / 8;
        String out = "expected/" + name + "-zoom.png";
        Raster r = SdgRender.render(sdg, 128, 128, sdg.width() * 0.1, sdg.height() * 0.1, upp);
        Files.write(dir.resolve(out), Png.write(r.width(), r.height(), r.rgba()));
        cases.add(String.format(Locale.ROOT, "%s 128 128 %s %s %s %s", file, num(sdg.width() * 0.1),
                num(sdg.height() * 0.1), num(upp), out));
    }

    private static void view(Path dir, String name, Sdg sdg, List<String> cases, String file, String label, int w,
                             int h) throws IOException {
        double upp = Math.max(sdg.width() / w, sdg.height() / h);
        double ox = (sdg.width() - w * upp) / 2, oy = (sdg.height() - h * upp) / 2;
        Raster r = SdgRender.render(sdg, w, h, ox, oy, upp);
        String out = "expected/" + name + "-" + label + ".png";
        Files.write(dir.resolve(out), Png.write(r.width(), r.height(), r.rgba()));
        cases.add(String.format(Locale.ROOT, "%s %d %d %s %s %s %s", file, w, h, num(ox), num(oy), num(upp), out));
    }

    /** The shortest decimal that reads back as the same double, so a reader in any language draws the same view. */
    private static String num(double v) {
        return Double.toString(v);
    }

    private static String read(String resource) throws IOException {
        try (InputStream in = Generate.class.getResourceAsStream(resource)) {
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        }
    }
}
