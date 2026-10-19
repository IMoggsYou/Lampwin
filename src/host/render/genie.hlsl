// MacWin genie renderer. Both styles come from the "MacOS Minimize Animation" Windhawk
// mod (MIT): the modern lamp curve is Potassiumuncher's CalculateLampVertexMacOS, the
// classic one the mod's original strip warp. Both are evaluated per vertex on the GPU
// instead of building ~1200 path geometries (or a per-scanline copy) per frame on the CPU.

cbuffer GenieCB : register(b0)
{
    float4 gWin;      // window rect: x, y, width, height (screen px)
    float4 gIcon;     // modern: taskbar icon rect x, y, width, height (screen px)
                      // classic: dock x, dock y, neck width, 1 = dock above the window
    float4 gCanvas;   // ghost canvas: left, top, width, height (screen px)
    float4 gParams;   // x = progress t, y = corner radius (snapshot px), z = tiles per side,
                      // w = global opacity
    float4 gTex;      // snapshot width, height (px), corner extent (px), corner exponent
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

// No vertex buffer: the (tiles+1)^2 grid vertices are derived from SV_VertexID.
GenieVSOut GenieVS(uint id : SV_VertexID)
{
    uint tiles = (uint)gParams.z;
    uint col = id % (tiles + 1);
    uint row = id / (tiles + 1);
    float tx = (float)col / gParams.z;
    float ty = (float)row / gParams.z;
    float2 s = LampVertexMacOS(tx, ty, gParams.x);
    float2 c = (s - gCanvas.xy) / gCanvas.zw;
    GenieVSOut o;
    o.pos = float4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    o.uv = float2(tx, ty);
    return o;
}

float ClassicMorph(float v, float tt)
{
    float m = saturate(tt * (1.0 + CLASSIC_SPREAD) - (1.0 - v) * CLASSIC_SPREAD);
    return m * m * (3.0 - 2.0 * m);
}

// Classic: every source row slides towards the dock edge and narrows to the neck.
GenieVSOut ClassicVS(uint id : SV_VertexID)
{
    uint tiles = (uint)gParams.z;
    uint col = id % (tiles + 1);
    uint row = id / (tiles + 1);
    float tx = (float)col / gParams.z;
    float ty = (float)row / gParams.z;
    float e = ClassicMorph(gIcon.w > 0.5 ? 1.0 - ty : ty, gParams.x);
    float idY = gWin.y + gWin.w * ty;
    float y = idY + (gIcon.y - idY) * e;
    float width = gWin.z + (gIcon.z - gWin.z) * e;
    float cx0 = gWin.x + gWin.z * 0.5;
    float cx = cx0 + (gIcon.x - cx0) * e;
    float2 s = float2(cx + (tx - 0.5) * width, y);
    float2 c = (s - gCanvas.xy) / gCanvas.zw;
    GenieVSOut o;
    o.pos = float4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    o.uv = float2(tx, ty);
    return o;
}

// Premultiplied snapshot (mipmapped, anisotropic) with rounded corners: a superellipse
// |x|^n + |y|^n = 1 spanning gTex.z px from each corner (see CornerShape in device.cpp).
// n = 2 is Windows 11's circular corner; larger n gives the smoothed macOS squircle.
float4 GeniePS(GenieVSOut i) : SV_Target
{
    float4 c = gSnapshot.Sample(gSampler, i.uv);
    float p = gTex.z;
    if (p > 0.0)
    {
        float2 halfSize = gTex.xy * 0.5;
        float2 qe = abs(i.uv * gTex.xy - halfSize) - halfSize;   // <= 0 inside the window
        float2 k = max(qe + p, 0.0);                             // depth into a corner box
        float d;
        if (k.x > 0.0 || k.y > 0.0)
        {
            // Distance to the curve, to first order: (m - 1) / |grad m|, m = ||k / p||_n.
            float n = gTex.w;
            float2 kn = k / p;
            float2 kp = pow(kn, n);
            float m = pow(kp.x + kp.y, 1.0 / n);
            float2 grad = (kp / max(kn, 1e-6)) / max(pow(m, n - 1.0), 1e-6);
            d = (m - 1.0) * p / max(length(grad), 1e-3);
        }
        else
        {
            d = max(qe.x, qe.y);
        }
        float aa = max(fwidth(d), 0.001);
        c *= saturate(0.5 - d / aa);
    }
    return c * gParams.w;
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
