// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin
// Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood
// and Potassiumuncher); see THIRD_PARTY_NOTICES.md.

// MacWin genie renderer. The genie styles come from the "MacOS Minimize Animation"
// Windhawk mod (MIT): the modern lamp curve is Potassiumuncher's CalculateLampVertexMacOS,
// the classic one the mod's original strip warp; the scale effect is MacWin's own. All
// are evaluated per vertex on the GPU instead of building ~1200 path geometries (or a
// per-scanline copy) per frame on the CPU. The window's shadow is a second pass over a
// grid that reaches past the window, warped by the same curve.

cbuffer GenieCB : register(b0)
{
    float4 gWin;      // window rect: x, y, width, height (screen px)
    float4 gIcon;     // modern: taskbar icon rect x, y, width, height (screen px)
                      // classic: dock x, dock y, neck width, 1 = dock above the window
                      // scale: the rect the window ends in
    float4 gCanvas;   // ghost canvas: left, top, width, height (screen px)
    float4 gParams;   // x = progress t, y = corner radius (snapshot px), z = tiles per side,
                      // w = global opacity
    float4 gTex;      // snapshot width, height (px), corner extent (px), corner exponent
    float4 gShadow;   // x = how far the grid reaches past the window (snapshot px; 0 for
                      // the window itself), y, z = broad shadow sigma, offset (snapshot px)
    float4 gShadow2;  // x, y = contact shadow sigma, offset (snapshot px), z, w = opacities
};

cbuffer PrepCB : register(b1)
{
    int4 gPrep;       // x, y = crop offset in the raw capture (px), z = force opaque alpha
};

Texture2D gSnapshot : register(t0);
Texture2D<float4> gRaw : register(t0);
SamplerState gSampler : register(s0);

static const float PI = 3.14159265;
static const float CLASSIC_SPREAD = 0.65;   // rows nearest the dock lead the morph

// Maps a mesh vertex (tx, ty in 0..1) at progress p to its warped screen position.
float2 LampVertexMacOS(float tx, float ty, float p)
{
    float split = 0.3;
    float k = (p <= split) ? (p / split) : 1.0;
    float j = (p > split) ? ((p - split) / (1.0 - split)) : 0.0;

    float expandHeight = (gIcon.y - gWin.y - gWin.w);
    float fullHeight = (gIcon.y - gWin.y) - (expandHeight * (1.0 - k));
    float height = fullHeight - (j * fullHeight);
    float safeFull = (abs(fullHeight) < 0.001) ? 0.001 : fullHeight;

    float y = ty * height;
    float x = tx * gIcon.z
            + tx * (gWin.z - gIcon.z) * (1.0 - j) * (1.0 - ty)
            + tx * (gWin.z - gIcon.z) * (1.0 - k) * ty;

    float offsetX = (gIcon.x - gWin.x) * (y / (fullHeight + 0.1)) * k + (gIcon.x - gWin.x) * j;
    float offsetY = gIcon.y - gWin.y - height - (expandHeight * (1.0 - k));

    float effectX = sin(((height - y) / safeFull) * 2.0 * PI + PI)
                  * (gWin.x + gWin.z * tx - (gIcon.x + gIcon.z * tx)) / 7.0 * k;

    return float2(gWin.x + x + offsetX + effectX, gWin.y + y + offsetY);
}

struct GenieVSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

// No vertex buffer: the (tiles+1)^2 grid vertices are derived from SV_VertexID. The
// shadow pass stretches the grid gShadow.x px past every edge of the window.
float2 GridUV(uint id)
{
    uint tiles = (uint)gParams.z;
    float2 g = float2(id % (tiles + 1), id / (tiles + 1)) / gParams.z;
    float2 margin = gShadow.x / gTex.xy;
    return g * (1.0 + 2.0 * margin) - margin;
}

GenieVSOut Output(float2 s, float2 uv)
{
    float2 c = (s - gCanvas.xy) / gCanvas.zw;
    GenieVSOut o;
    o.pos = float4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}

GenieVSOut GenieVS(uint id : SV_VertexID)
{
    float2 uv = GridUV(id);
    return Output(LampVertexMacOS(uv.x, uv.y, gParams.x), uv);
}

// Scale: the window shrinks into its end rect (gIcon). The size leads and both settle
// together (ScaleVertex in genie_effect.cpp).
GenieVSOut ScaleVS(uint id : SV_VertexID)
{
    float2 uv = GridUV(id);
    float t = gParams.x;
    float size = 1.0 - (1.0 - t) * (1.0 - t);
    float pos = t * t * (3.0 - 2.0 * t);
    float2 from = gWin.xy + gWin.zw * 0.5;
    float2 to = gIcon.xy + gIcon.zw * 0.5;
    float2 dim = gWin.zw + (gIcon.zw - gWin.zw) * size;
    return Output(from + (to - from) * pos + (uv - 0.5) * dim, uv);
}

float ClassicMorph(float v, float tt)
{
    float m = saturate(tt * (1.0 + CLASSIC_SPREAD) - (1.0 - v) * CLASSIC_SPREAD);
    return m * m * (3.0 - 2.0 * m);
}

// Classic: every source row slides towards the dock edge and narrows to the neck.
GenieVSOut ClassicVS(uint id : SV_VertexID)
{
    float2 uv = GridUV(id);
    float e = ClassicMorph(gIcon.w > 0.5 ? 1.0 - uv.y : uv.y, gParams.x);
    float idY = gWin.y + gWin.w * uv.y;
    float y = idY + (gIcon.y - idY) * e;
    float width = gWin.z + (gIcon.z - gWin.z) * e;
    float cx0 = gWin.x + gWin.z * 0.5;
    float cx = cx0 + (gIcon.x - cx0) * e;
    return Output(float2(cx + (uv.x - 0.5) * width, y), uv);
}

// Signed distance (snapshot px, < 0 inside) to the window's shape: a superellipse
// |x|^n + |y|^n = 1 spanning gTex.z px from each corner (see common/corner_shape.h).
// n = 2 is Windows 11's circular corner; larger n gives the smoothed macOS squircle.
float WindowDistance(float2 uv)
{
    float2 halfSize = gTex.xy * 0.5;
    float2 qe = abs(uv * gTex.xy - halfSize) - halfSize;   // <= 0 inside the window
    float p = gTex.z;
    float2 k = max(qe + p, 0.0);                           // depth into a corner box
    if (p > 0.0 && (k.x > 0.0 || k.y > 0.0))
    {
        // Distance to the curve, to first order: (m - 1) / |grad m|, m = ||k / p||_n.
        float n = gTex.w;
        float2 kn = k / p;
        float2 kp = pow(kn, n);
        float m = pow(kp.x + kp.y, 1.0 / n);
        float2 grad = (kp / max(kn, 1e-6)) / max(pow(m, n - 1.0), 1e-6);
        return (m - 1.0) * p / max(length(grad), 1e-3);
    }
    return max(qe.x, qe.y);
}

// Premultiplied snapshot (mipmapped, anisotropic) cut to the window's shape.
float4 GeniePS(GenieVSOut i) : SV_Target
{
    float4 c = gSnapshot.Sample(gSampler, i.uv);
    if (gTex.z > 0.0)
    {
        float d = WindowDistance(i.uv);
        float aa = max(fwidth(d), 0.001);
        c *= saturate(0.5 - d / aa);
    }
    return c * gParams.w;
}

// Exact distance to a plain rounded rectangle around 0: the shadow's shape (blurred,
// the squircle's difference does not show).
float RoundRectDistance(float2 p, float2 halfSize, float r)
{
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// erfc to ~1e-4 (Winitzki's approximation of erf).
float Erfc(float x)
{
    float x2 = x * x;
    float a = 0.147;
    float erfAbs = sqrt(1.0 - exp(-x2 * (1.27323954 + a * x2) / (1.0 + a * x2)));
    return 1.0 - sign(x) * erfAbs;
}

// The window's shadow (mw::WindowShadow, as the shade of a window cut to its corners
// draws it): a broad and a contact layer, nothing under the window itself.
float4 ShadowPS(GenieVSOut i) : SV_Target
{
    float2 halfSize = gTex.xy * 0.5;
    float2 p = i.uv * gTex.xy - halfSize;
    float r = min(gParams.y, min(halfSize.x, halfSize.y));
    float cover = saturate(0.5 - WindowDistance(i.uv));
    float s1 = gShadow2.z * 0.5 * Erfc(RoundRectDistance(p - float2(0.0, gShadow.z), halfSize, r) / (gShadow.y * 1.41421356));
    float s2 = gShadow2.w * 0.5 * Erfc(RoundRectDistance(p - float2(0.0, gShadow2.y), halfSize, r) / (gShadow2.x * 1.41421356));
    float a = (1.0 - (1.0 - s1) * (1.0 - s2)) * (1.0 - cover);
    return float4(0.0, 0.0, 0.0, a * gParams.w);
}

// One-off pass per snapshot: crop the raw capture to the extended frame and fix its
// alpha, then mipmaps are generated from the result.
float4 FullscreenVS(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PrepPS(float4 pos : SV_Position) : SV_Target
{
    float4 c = gRaw.Load(int3(int2(pos.xy) + gPrep.xy, 0));
    // The mod's fallback: when most of the window came back fully transparent the
    // alpha is not describing real translucency (GDI apps, Mica on secondary
    // monitors), so those pixels are made opaque instead of becoming a hole.
    if (gPrep.z != 0 && c.a == 0.0) c.a = 1.0;
    // Keep the premultiplied invariant: PrintWindow leaves stray colour under alpha 0.
    c.rgb = min(c.rgb, c.aaa);
    return c;
}
