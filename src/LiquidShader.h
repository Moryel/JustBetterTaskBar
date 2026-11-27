// Liquid glass shader and its parameters, shared by the dock and the preview tool.
#pragma once
#include <algorithm>
#include <cmath>

namespace liquid {

static const char* kShader = R"(
cbuffer C : register(b0) {
    float2 size; float2 offs;
    float2 tex; float radius; float scale;
    float sat; float refr; float edge; float spacing;
    float lod; float light; float mode; float nearTint;
    float2 inward; float nearDist; float unused;
};
Texture2D src : register(t0);
SamplerState smp : register(s0);

float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float sd(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0) + length(max(q, 0)) - r;
}
float3 blurAt(float2 px) {
    float3 c = 0;
    float w = 0;
    [unroll] for (int y = -2; y <= 2; y++)
    [unroll] for (int x = -2; x <= 2; x++) {
        float k = exp(-(x * x + y * y) / 4.0);
        c += src.SampleLevel(smp, (px + float2(x, y) * spacing) / tex, lod).rgb * k;
        w += k;
    }
    return c / w;
}
// Color of whatever lies right next to the dock on the screen side (usually the open window).
float3 nearColor(float2 pos) {
    float2 far = float2(inward.x > 0.5 ? size.x : (inward.x < -0.5 ? 0 : pos.x),
                        inward.y > 0.5 ? size.y : (inward.y < -0.5 ? 0 : pos.y));
    return src.SampleLevel(smp, (offs + far + inward * nearDist) / tex, 4).rgb;
}
float4 PS(float4 pos : SV_Position) : SV_Target {
    float2 h = size * 0.5;
    float2 p = pos.xy - h;
    float r = min(radius, min(h.x, h.y));
    float d = sd(p, h, r);
    float2 n = float2(sd(p + float2(1, 0), h, r) - sd(p - float2(1, 0), h, r),
                      sd(p + float2(0, 1), h, r) - sd(p - float2(0, 1), h, r));
    n /= max(length(n), 1e-4);
    float depth = -d;

    // clear mode, fitted to a macOS 27 dock: no blur, the rim magnifies the backdrop from
    // further inside, a veil made from the darkened backdrop color lowers contrast, a bright
    // line runs along the edge (strongest on top) with a faint dark line inside it on the sides
    if (mode > 0.5) {
        float tc = saturate(1 - depth / edge);
        float2 q = offs + pos.xy + n * refr * tc * tc * tc;
        float3 cc = src.SampleLevel(smp, q / tex, 0).rgb;
        float3 avg = nearTint > 0.5 ? nearColor(pos.xy) : src.SampleLevel(smp, (offs + pos.xy) / tex, 4).rgb;
        float3 veil = light > 0.5 ? lerp(0.90.xxx, avg, 0.2) : float3(0.006, 0.012, 0.056) + avg * 0.568;
        cc = cc * 0.69 + veil * 0.31;
        float up = max(-n.y, 0), dn = max(n.y, 0);
        float rimAmp = 0.174 * up * up + 0.142 * dn * dn + 0.058 * n.x * n.x;
        cc += rimAmp * saturate(1 - abs(depth - 0.4 * scale) / (0.986 * scale));
        cc -= 0.052 * n.x * n.x * saturate(1 - abs(depth - 2.1 * scale) / scale);
        cc -= 0.014 * tc;
        return float4(saturate(cc), 1);
    }

    // refraction: the rounded rim pulls the backdrop from just outside the shape inwards
    float t = saturate(1 - depth / edge);
    float bend = refr * pow(t, 2.5);
    float3 c = blurAt(offs + pos.xy + n * bend);

    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(l.xxx, c, sat);
    c = light > 0.5 ? c * 0.86 + 0.20 : c * 0.62 + 0.035;
    if (nearTint > 0.5) c = lerp(c, nearColor(pos.xy) * (light > 0.5 ? 1.0 : 0.7), 0.35);

    // rim: a faint dark outline, a bright specular line just inside it, and a soft inner glow
    float facing = abs(dot(n, normalize(float2(-0.35, -1))));
    float outline = saturate(1 - depth / (1.0 * scale));
    float spec = saturate(1 - abs(depth - 1.6 * scale) / (1.0 * scale));
    float glow = saturate(1 - depth / (7 * scale));
    c *= 1 - outline * (light > 0.5 ? 0.22 : 0.35);
    c += spec * (light > 0.5 ? 0.22 : 0.16) * (0.35 + 0.65 * facing * facing);
    c += glow * glow * (light > 0.5 ? 0.07 : 0.05) * (0.5 + 0.5 * facing);
    return float4(saturate(c), 1);
}
)";

struct CB {
    float size[2], offs[2];
    float tex[2], radius, scale;
    float sat, refr, edge, spacing;
    float lod, light, mode, nearTint;
    float inward[2], nearDist, unused;
};

// w, h: glass size; ox, oy: glass top-left inside the backdrop texture; tw, th: backdrop size.
// mode 0: frosted liquid glass, mode 1: clear (fitted to macOS 27)
inline CB MakeCB(int w, int h, float ox, float oy, int tw, int th, float radius, float scale, bool light, float sat, int mode = 0,
                 float inX = 0, float inY = -1, bool nearTint = false) {
    CB c{};
    c.size[0] = (float)w;
    c.size[1] = (float)h;
    c.offs[0] = ox;
    c.offs[1] = oy;
    c.tex[0] = (float)tw;
    c.tex[1] = (float)th;
    c.radius = radius;
    c.scale = scale;
    c.sat = std::max(sat, 1.15f);
    c.refr = 9 * scale;
    c.edge = std::min(12 * scale, h * 0.4f);
    c.lod = std::log2(std::max(scale * 4.0f, 1.0f));
    c.spacing = std::exp2(c.lod) * 0.75f;
    c.light = light ? 1.0f : 0.0f;
    c.mode = (float)mode;
    // inward: unit vector from the screen edge towards the screen's middle
    c.nearTint = nearTint ? 1.0f : 0.0f;
    c.inward[0] = inX;
    c.inward[1] = inY;
    c.nearDist = 18 * scale + 8;
    if (mode == 1) {
        float t = (float)std::min(w, h);
        c.edge = 0.29f * t;
        c.refr = -0.32f * t;
    }
    return c;
}

// How far outside the glass the shader reads.
inline int Pad(float scale) {
    float texel = std::max(scale * 4.0f, 1.0f);
    return (int)std::ceil(9 * scale + 2 * texel * 0.75f + texel + 4);
}

}  // namespace liquid
