package dev.vexelray.sdg.shaders;

import dev.supirvast.vastir.core.BinaryOp;
import dev.supirvast.vastir.core.Builtin;
import dev.supirvast.vastir.core.CoreModule;
import dev.supirvast.vastir.core.EntryPoint;
import dev.supirvast.vastir.core.Expr;
import dev.supirvast.vastir.core.Function;
import dev.supirvast.vastir.core.InterfaceVar;
import dev.supirvast.vastir.core.LocalVar;
import dev.supirvast.vastir.core.PushConstants;
import dev.supirvast.vastir.core.Region;
import dev.supirvast.vastir.core.ShaderStage;
import dev.supirvast.vastir.core.Statement;
import dev.supirvast.vastir.core.Texture;
import dev.supirvast.vastir.lower.CoreToSpirv;
import dev.supirvast.vastir.type.Type;

import java.util.ArrayList;
import java.util.List;

/**
 * SDG's shaders in SupirVast's core IR: what a GPU host draws a drawing with, in any graphics API, by the spec's §2.
 *
 * <ul>
 *   <li><b>{@code sdg_quad.vert}</b>: a quad from {@code gl_VertexIndex} alone (six vertices, no vertex buffer), its
 *       corners from the push-constant block, carrying the canvas point to the fragment.</li>
 *   <li><b>{@code sdg_layer.frag}</b>: one shape layer: the stencil's distance read between texels (a channel for an
 *       SDF, the median of three for an MSDF), cut half a pixel either side, times the paint (a colour, or a texture
 *       read between its texels inside its rectangle), out premultiplied, for {@code ONE, ONE_MINUS_SRC_ALPHA}.</li>
 *   <li><b>{@code sdg_group.frag}</b>: an SDG 1.2 group laid over: its target (drawn apart, premultiplied) read at the
 *       fragment, times its opacity and its mask, read beside it in the same target as luminance or alpha.</li>
 * </ul>
 *
 * The push-constant block is eight {@code vec4}, 128 bytes, the most every Vulkan device takes; its layout is the
 * {@code LAYER} and {@code GROUP} member lists below, and README.md's tables.
 */
public final class SdgShaders {

    static final Type.Float F32 = Type.float32();
    static final Type.Vector V2 = new Type.Vector(F32, 2), V4 = new Type.Vector(F32, 4);

    /** A layer's block: the quad (vertex), then the stencil and the paint (fragment). */
    static final PushConstants LAYER = new PushConstants(List.of(
            new PushConstants.Member("clipRect", V4),      // the quad in clip space: x0, y0, x1, y1
            new PushConstants.Member("canvasRect", V4),    // the same quad in canvas units: x0, y0, x1, y1
            new PushConstants.Member("stencilPlace", V4),  // originX, originY, scale (texels a unit), range (texels)
            new PushConstants.Member("stencilRect", V4),   // the stencil's rectangle on its page: x, y, width, height
            new PushConstants.Member("stencilPage", V4),   // 1 / page width, 1 / page height, mode, unitsPerPixel
            new PushConstants.Member("colour", V4),        // straight RGBA, its alpha times the layer's opacity
            new PushConstants.Member("paintMap", V4),      // paint uv = canvas * (x, y) + (z, w); x == 0: solid
            new PushConstants.Member("paintClamp", V4)));  // the paint's rectangle in uv, half a texel in

    /** A group's block: the quad (vertex), then the target's mapping, opacity and mask. */
    static final PushConstants GROUP = new PushConstants(List.of(
            new PushConstants.Member("clipRect", V4),
            new PushConstants.Member("uvRect", V4),        // the group's rectangle of its target, in uv: u0, v0, u1, v1
            new PushConstants.Member("mask", V4)));        // mask u offset, kind (0 none, 1 luminance, 2 alpha), opacity

    /** Binding 0: the stencil's page. Binding 1: the paint's page (the same image again when they share one). */
    static final Texture STENCIL_PAGE = new Texture("stencilPage", 0, 0), PAINT_PAGE = new Texture("paintPage", 0, 1);

    /** Binding 0 of a group's pass: the target it was drawn into. */
    static final Texture GROUP_TARGET = new Texture("groupTarget", 0, 0);

    private SdgShaders() {
    }

    // ------------------------------------------------------------------------------------------------ helpers

    static Expr f(double v) {
        return new Expr.ConstFloat(F32, v);
    }

    static Expr i(long v) {
        return new Expr.ConstInt(Type.int32(), v);
    }

    static Expr add(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.ADD, a, b);
    }

    static Expr sub(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.SUB, a, b);
    }

    static Expr mul(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.MUL, a, b);
    }

    static Expr div(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.DIV, a, b);
    }

    static Expr lt(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.LESS_THAN, a, b);
    }

    static Expr gt(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.GREATER_THAN, a, b);
    }

    static Expr or(Expr a, Expr b) {
        return new Expr.Binary(BinaryOp.LOGICAL_OR, a, b);
    }

    static Expr x(Expr v) {
        return new Expr.VectorExtract(v, 0);
    }

    static Expr y(Expr v) {
        return new Expr.VectorExtract(v, 1);
    }

    static Expr z(Expr v) {
        return new Expr.VectorExtract(v, 2);
    }

    static Expr w(Expr v) {
        return new Expr.VectorExtract(v, 3);
    }

    static Expr v2(Expr a, Expr b) {
        return new Expr.VectorConstruct(V2, List.of(a, b));
    }

    static Expr v4(Expr a, Expr b, Expr c, Expr d) {
        return new Expr.VectorConstruct(V4, List.of(a, b, c, d));
    }

    static Expr clamp(Expr v, Expr lo, Expr hi) {
        return Expr.MathCall.clamp(v, lo, hi);
    }

    static Expr min(Expr a, Expr b) {
        return Expr.MathCall.min(a, b);
    }

    static Expr max(Expr a, Expr b) {
        return Expr.MathCall.max(a, b);
    }

    static Expr read(LocalVar v) {
        return new Expr.Read(v);
    }

    static byte[] lower(Function main, ShaderStage stage) {
        return new CoreToSpirv().lower(new CoreModule().addEntryPoint(EntryPoint.of(main, stage))).toByteArray();
    }

    // ------------------------------------------------------------------------------------------------ the quad

    /**
     * Six vertices, two triangles, from {@code gl_VertexIndex}: corner {@code k} of (0,0) (1,0) (1,1) (0,0) (1,1)
     * (0,1), placed by {@code clipRect}; the canvas point, by {@code canvasRect}, out at location 0.
     */
    static byte[] quadVertex(PushConstants block, boolean canvas) {
        Expr idx = new Expr.BuiltinRead(Builtin.VERTEX_INDEX);
        // corner x: vertices 1, 2, 4 are on the right; corner y: 2, 4, 5 at the bottom
        Expr cx = new Expr.Convert(new Expr.Binary(BinaryOp.BIT_AND,
                new Expr.Binary(BinaryOp.SHIFT_RIGHT, i(0b010110), idx), i(1)), F32);
        Expr cy = new Expr.Convert(new Expr.Binary(BinaryOp.BIT_AND,
                new Expr.Binary(BinaryOp.SHIFT_RIGHT, i(0b110100), idx), i(1)), F32);
        Expr clip = block.read(0);
        Expr px = add(x(clip), mul(sub(z(clip), x(clip)), cx));
        Expr py = add(y(clip), mul(sub(w(clip), y(clip)), cy));
        List<Statement> body = new ArrayList<>();
        body.add(new Statement.BuiltinWrite(Builtin.POSITION, v4(px, py, f(0), f(1))));
        InterfaceVar out = InterfaceVar.output("vPoint", 0, V2);
        Expr rect = block.read(1);
        body.add(new Statement.InterfaceWrite(out, v2(add(x(rect), mul(sub(z(rect), x(rect)), cx)),
                add(y(rect), mul(sub(w(rect), y(rect)), cy)))));
        body.add(new Statement.ReturnVoid());
        return lower(new Function("main", new Type.FunctionType(Type.VOID, List.of()), new Region(body)),
                ShaderStage.VERTEX);
    }

    // ------------------------------------------------------------------------------------------------ a layer

    /** One shape layer (spec §2, steps 1 to 5), out premultiplied. */
    static byte[] layerFragment() {
        InterfaceVar point = InterfaceVar.input("vPoint", 0, V2);
        InterfaceVar fragColor = InterfaceVar.output("fragColor", 0, V4);
        Expr p = new Expr.InterfaceRead(point);
        Expr place = LAYER.read(2), rect = LAYER.read(3), page = LAYER.read(4);
        Expr colour = LAYER.read(5), map = LAYER.read(6), clampRect = LAYER.read(7);

        List<Statement> body = new ArrayList<>();
        // 1. texel coordinates in the stencil's rectangle
        LocalVar t = new LocalVar("t", V2);
        body.add(new Statement.DeclareVar(t, v2(mul(sub(x(p), x(place)), z(place)),
                mul(sub(y(p), y(place)), z(place)))));
        LocalVar out = new LocalVar("result", V4);
        body.add(new Statement.DeclareVar(out, v4(f(0), f(0), f(0), f(0))));

        List<Statement> inside = new ArrayList<>();
        // 2. clamped half a texel in, read between texel centres
        Expr tc = v2(clamp(x(read(t)), f(0.5), sub(z(rect), f(0.5))), clamp(y(read(t)), f(0.5), sub(w(rect), f(0.5))));
        Expr uv = v2(mul(add(x(rect), x(tc)), x(page)), mul(add(y(rect), y(tc)), y(page)));
        LocalVar s = new LocalVar("s", V4);
        inside.add(new Statement.DeclareVar(s, new Expr.SampleTexture(STENCIL_PAGE, uv, f(0))));
        // the distance's encoded value: a channel of an SDF (mode 0 to 3), the median of three for an MSDF (mode 4)
        Expr mode = z(page);
        LocalVar e = new LocalVar("e", F32);
        Expr sr = x(read(s)), sg = y(read(s)), sb = z(read(s)), sa = w(read(s));
        Expr median = max(min(sr, sg), min(max(sr, sg), sb));
        // one-hot by mode: SDF channel c weighs 1 where |mode - c| < 0.5
        Expr pick = add(add(mul(sr, onehot(mode, 0)), mul(sg, onehot(mode, 1))),
                add(mul(sb, onehot(mode, 2)), mul(sa, onehot(mode, 3))));
        inside.add(new Statement.DeclareVar(e, Expr.MathCall.mix(pick, median, Expr.MathCall.step(f(3.5), mode))));
        // 3. coverage: the distance in pixels, cut half a pixel either side
        Expr d = mul(sub(read(e), f(0.5)), w(place));
        Expr cover = clamp(add(f(0.5), div(div(d, z(place)), w(page))), f(0), f(1));
        LocalVar a = new LocalVar("cover", F32);
        inside.add(new Statement.DeclareVar(a, cover));
        // 4. colour: the vertex colour, times a texel of the paint for a sampled one
        LocalVar c = new LocalVar("paint", V4);
        inside.add(new Statement.DeclareVar(c, colour));
        Expr puv = v2(clamp(add(mul(x(p), x(map)), z(map)), x(clampRect), z(clampRect)),
                clamp(add(mul(y(p), y(map)), w(map)), y(clampRect), w(clampRect)));
        Expr sampled = new Expr.SampleTexture(PAINT_PAGE, puv, f(0));
        inside.add(new Statement.If(gt(Expr.MathCall.abs(x(map)), f(0)),
                Region.of(new Statement.Assign(c, mul(colour, sampled))), Region.of()));
        // 5. premultiplied, for ONE, ONE_MINUS_SRC_ALPHA
        Expr alpha = mul(read(a), w(read(c)));
        inside.add(new Statement.Assign(out, v4(mul(x(read(c)), alpha), mul(y(read(c)), alpha),
                mul(z(read(c)), alpha), alpha)));

        // past the rectangle, the layer does not cover the point
        Expr outside = or(or(lt(x(read(t)), f(0)), lt(y(read(t)), f(0))),
                or(gt(x(read(t)), z(rect)), gt(y(read(t)), w(rect))));
        body.add(new Statement.If(outside, Region.of(), new Region(inside)));
        body.add(new Statement.InterfaceWrite(fragColor, read(out)));
        body.add(new Statement.ReturnVoid());
        return lower(new Function("main", new Type.FunctionType(Type.VOID, List.of()), new Region(body)),
                ShaderStage.FRAGMENT);
    }

    /** 1 where {@code mode} is {@code c} (within half), else 0. */
    static Expr onehot(Expr mode, int c) {
        Expr off = Expr.MathCall.abs(sub(mode, f(c)));
        return sub(f(1), Expr.MathCall.step(f(0.5), off));
    }

    // ------------------------------------------------------------------------------------------------ a group

    /** A group laid over: target premultiplied, made straight, times opacity and mask, out premultiplied again. */
    static byte[] groupFragment() {
        InterfaceVar point = InterfaceVar.input("vPoint", 0, V2);
        InterfaceVar fragColor = InterfaceVar.output("fragColor", 0, V4);
        Expr uv = new Expr.InterfaceRead(point);
        Expr mask = GROUP.read(2);
        List<Statement> body = new ArrayList<>();
        LocalVar g = new LocalVar("group", V4), m = new LocalVar("mask", V4);
        body.add(new Statement.DeclareVar(g, new Expr.SampleTexture(GROUP_TARGET, uv, f(0))));
        body.add(new Statement.DeclareVar(m, new Expr.SampleTexture(GROUP_TARGET,
                v2(add(x(uv), x(mask)), y(uv)), f(0))));
        Expr kind = y(mask);
        Expr luma = add(add(mul(f(0.2126), x(read(m))), mul(f(0.7152), y(read(m)))), mul(f(0.0722), z(read(m))));
        Expr k = Expr.MathCall.mix(f(1), Expr.MathCall.mix(luma, w(read(m)), Expr.MathCall.step(f(1.5), kind)),
                Expr.MathCall.step(f(0.5), kind));
        Expr factor = mul(z(mask), k);
        // the target holds premultiplied colour: laid over premultiplied, it is the target times the factor
        body.add(new Statement.InterfaceWrite(fragColor, v4(mul(x(read(g)), factor), mul(y(read(g)), factor),
                mul(z(read(g)), factor), mul(w(read(g)), factor))));
        body.add(new Statement.ReturnVoid());
        return lower(new Function("main", new Type.FunctionType(Type.VOID, List.of()), new Region(body)),
                ShaderStage.FRAGMENT);
    }
}
