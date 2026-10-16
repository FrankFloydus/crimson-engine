// Six perspective faces per point light, arranged in guarded 3x2 atlas blocks.
// D32F uses ordinary zero-to-one perspective depth on the D3D11 renderer.
uniform vec4 u_pointShadow[16]; // Atlas slot (-1 disables), near, bias(m), normal bias(texels).
uniform vec4 u_shadowAtlas; // Reciprocal atlas extent, inner face resolution, guarded pitch.
uniform vec4 u_shadowLayout; // Tile columns, guarded tan(half FOV), reserved.
#ifdef CC_POINT_SHADOW_SLOT
SAMPLER2DSHADOW(s_pointShadow, CC_POINT_SHADOW_SLOT);
#else
SAMPLER2DSHADOW(s_pointShadow, 8);
#endif

// Evaluate before discard or divergent lighting branches. The triangle plane
// must remain independent of smooth vertex normals and normal-map shading.
#ifndef CC_SHADOW_COMPUTE
vec3 pointShadowPlaneNormal(vec3 position, vec3 fallback)
{
    vec3 dx = dFdx(position);
    vec3 dy = dFdy(position);
    dx *= inversesqrt(max(dot(dx, dx), 0.00000000000000000001));
    dy *= inversesqrt(max(dot(dy, dy), 0.00000000000000000001));
    vec3 normal = cross(dx, dy);
    float squared = dot(normal, normal);
    return squared > 0.000000000001 ? normal * inversesqrt(squared) : fallback;
}
#endif

float pointShadowVisibilityPcf(int lightIndex, vec4 lightPosition, vec4 lightColor, vec3 position, vec3 geometricNormal, vec3 planeNormal)
{
    if (lightIndex < 0) return 1.0;
    vec4 settings = u_pointShadow[lightIndex];
    if (settings.x < 0.0 || u_shadowAtlas.z < 1.0) return 1.0;
    vec3 relative = position - lightPosition.xyz;
    float radial = length(relative);
    float farPlane = inversesqrt(max(lightColor.w, 1.0 / (100000.0 * 100000.0)));
    if (radial <= settings.y || radial >= farPlane) return 1.0;
    vec3 L = -relative / radial;
    float axial = max(max(abs(relative.x), abs(relative.y)), abs(relative.z));
    float texelWorld = 2.0 * axial / u_shadowAtlas.z;
    float normalCosine = clamp(dot(geometricNormal, L), 0.0, 1.0);
    // Normal offset scales with sin(theta), not 1-cos(theta): the latter
    // loses protection quadratically around the directly facing point.
    relative += geometricNormal * (settings.w * texelWorld * sqrt(max(0.0, 1.0 - normalCosine * normalCosine)));
    vec3 magnitude = abs(relative);
    vec2 coordinates;
    vec2 planeAxes; // -dot(plane normal, face right), dot(plane normal, face up).
    float face;
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        face = relative.x >= 0.0 ? 0.0 : 1.0;
        coordinates = vec2(relative.x >= 0.0 ? relative.z : -relative.z, -relative.y) / magnitude.x;
        planeAxes = vec2(relative.x >= 0.0 ? -planeNormal.z : planeNormal.z, planeNormal.y);
        axial = magnitude.x;
    }
    else if (magnitude.y >= magnitude.z)
    {
        face = relative.y >= 0.0 ? 2.0 : 3.0;
        coordinates = vec2(-relative.x, relative.y >= 0.0 ? relative.z : -relative.z) / magnitude.y;
        planeAxes = vec2(planeNormal.x, relative.y >= 0.0 ? -planeNormal.z : planeNormal.z);
        axial = magnitude.y;
    }
    else
    {
        face = relative.z >= 0.0 ? 4.0 : 5.0;
        coordinates = vec2(relative.z >= 0.0 ? -relative.x : relative.x, -relative.y) / magnitude.z;
        planeAxes = vec2(relative.z >= 0.0 ? planeNormal.x : -planeNormal.x, planeNormal.y);
        axial = magnitude.z;
    }
    // Apply a world-space radial depth bias, then convert to the perspective
    // depth written by this face. Normal maps never perturb this bias normal.
    float biasScale = max(0.0, 1.0 - settings.z / max(length(relative), 0.000001));
    float biasedAxial = axial * biasScale;
    if (biasedAxial <= settings.y) return 1.0;
    float depth = farPlane / (farPlane - settings.y) - farPlane * settings.y / ((farPlane - settings.y) * biasedAxial);
    // Perspective depth is affine in face UV for a planar receiver. Correct
    // each PCF tap to that plane instead of comparing all nine depths at the
    // center: the latter falsely shadows sloped faces and exposes cube seams.
    float planeDistance = dot(planeNormal, relative) * biasScale;
    vec2 depthGradient = vec2_splat(0.0);
    if (abs(planeDistance) > max(radial * 0.000001, 0.000001))
        depthGradient = planeAxes * (2.0 * farPlane * settings.y /
            ((farPlane - settings.y) * u_shadowAtlas.z * planeDistance));
    float blockColumns = u_shadowLayout.x / 3.0;
    float blockY = floor(settings.x / blockColumns);
    float blockX = settings.x - blockY * blockColumns;
    float faceY = floor(face / 3.0);
    vec2 tile = vec2(blockX * 3.0 + face - faceY * 3.0, blockY * 2.0 + faceY);
    vec2 origin = tile * u_shadowAtlas.w;
    // Two guard texels are rendered with an expanded projection, so every
    // 3x3 bilinear comparison footprint contains real geometry across edges.
    vec2 pixel = origin + vec2_splat(2.0) + (coordinates * 0.5 + 0.5) * u_shadowAtlas.z;
    // Bound numerical comparison error separately from authored metre/texel
    // offsets. gamma(8) covers the perspective coefficients, divide and plane
    // reference arithmetic, using IEEE single-precision unit roundoff 2^-24.
    // One ULP per world input also accounts for absolute-coordinate rounding;
    // propagate that world-plane error through the perspective-depth derivative.
    // A fixed number of D32 units alone misses translated near-light surfaces.
    float unitRoundoff = 0.000000059604644775390625;
    float gamma8 = (8.0 * unitRoundoff) / (1.0 - 8.0 * unitRoundoff);
    float inverseDepthTerm = farPlane * settings.y / ((farPlane - settings.y) * biasedAxial);
    vec3 worldRoundoff = (2.0 * unitRoundoff) * (abs(position) + abs(lightPosition.xyz));
    float relativePlaneRoundoff = abs(planeDistance) > max(radial * 0.000001, 0.000001) ?
        dot(abs(planeNormal), worldRoundoff) / abs(planeDistance) :
        max(max(worldRoundoff.x, worldRoundoff.y), worldRoundoff.z) / biasedAxial;
    float comparisonTolerance = gamma8 * (abs(farPlane / (farPlane - settings.y)) + abs(inverseDepthTerm)) +
        abs(inverseDepthTerm) * relativePlaneRoundoff;
    float visibility = 0.0;
    // The nine bilinear taps cover only sixteen unique texels. Compare each
    // stored depth against the receiver plane at that SAME texel center, then
    // apply the equivalent separable weights. A single shallow reference for
    // four different centers turns an adjacent wall into light at a join.
    vec2 lower = floor(pixel - vec2_splat(0.5)) + vec2_splat(0.5);
    vec2 fraction = pixel - lower;
    vec4 weightsX = vec4(1.0 - fraction.x, 1.0, 1.0, fraction.x) / 3.0;
    vec4 weightsY = vec4(1.0 - fraction.y, 1.0, 1.0, fraction.y) / 3.0;
    vec2 central = floor(pixel) + vec2_splat(0.5);
    float centralVisibility = 1.0;
    float centralReceiver = 0.0;
    float contactSeparationSquared = 16.0;
    float storedDepths[16];
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 center = clamp(lower + vec2(float(x - 1), float(y - 1)), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
            float reference = depth + dot(depthGradient, center - pixel);
            float stored = s_pointShadow.m_texture.Load(ivec3(ivec2(center), 0)).x;
            storedDepths[y * 4 + x] = stored;
            float sampleVisibility = reference <= stored + comparisonTolerance ? 1.0 : 0.0;
            float weight = weightsX[x] * weightsY[y];
            visibility += sampleVisibility * weight;
            if (center.x == central.x && center.y == central.y)
            {
                centralVisibility = sampleVisibility;
                centralReceiver = stored > 0.0 && stored < 1.0 && abs(reference - stored) <= comparisonTolerance ? 1.0 : 0.0;
            }
            // Decode the physical gap only for a real blocker on this texel's
            // own receiver-plane ray. The ratio below is algebraically
            // (receiverAxial-blockerAxial)*rayLength/(2*receiverAxial/resolution),
            // avoiding two reciprocal perspective-depth reconstructions.
            if (weight > 0.0 && stored > 0.0 && stored < 1.0 && reference < 1.0 &&
                reference > stored + comparisonTolerance)
            {
                float numerator = (reference - stored) * (farPlane - settings.y) * (0.5 * u_shadowAtlas.z);
                float denominator = max(farPlane - stored * (farPlane - settings.y), settings.y);
                // Ray length is at least one. Reject farther blockers before
                // dividing; only a sub-four-texel axial gap can tighten PCF.
                if (numerator < 4.0 * denominator)
                {
                    vec2 coordinatesAtCenter = ((center - origin - vec2_splat(2.0)) / u_shadowAtlas.z) * 2.0 - 1.0;
                    float axialSeparation = numerator / denominator;
                    float separationSquared = axialSeparation * axialSeparation * (1.0 + dot(coordinatesAtCenter, coordinatesAtCenter));
                    contactSeparationSquared = min(contactSeparationSquared, separationSquared);
                }
            }
        }
    }
    // Near a projected join, the closest texel can miss the blocker even
    // though the actual fragment ray intersects it. Perspective depth of a
    // planar blocker is affine in shadow pixels, just like the receiver.
    // Recover that depth from a coherent 2x2 patch already in the footprint.
    // This is only a one-shadow-texel coverage completion at mixed contact
    // edges. Observed receiver coverage below rejects a remote finite edge
    // whose extrapolated plane would incorrectly extend across that receiver.
    // gamma(32) bounds the weight products and sixteen-term accumulation;
    // a fully lit sum rounded just below one is not a mixed footprint.
    float filterUnitRoundoff = 0.000000059604644775390625;
    float filterRoundoff = (32.0 * filterUnitRoundoff) / (1.0 - 32.0 * filterUnitRoundoff);
    if (visibility > filterRoundoff && visibility < 1.0 - filterRoundoff)
    {
        float originalPlaneDistance = dot(planeNormal, position - lightPosition.xyz);
        bool originalPlaneValid = abs(originalPlaneDistance) > max(radial * 0.000001, 0.000001);
        float originalReferenceRatio = originalPlaneValid ? planeDistance / originalPlaneDistance : 0.0;
        float perspectiveConstant = farPlane / (farPlane - settings.y);
        float bestExtrapolation = 3.0;
        float reconstructedDepth = 1.0;
        float reconstructedContact = 4.0;
        bool reconstructed = false;
        for (int y = 0; y < 3; ++y)
        {
            for (int x = 0; x < 3; ++x)
            {
                if (!originalPlaneValid) continue;
                vec2 firstCenter = lower + vec2(float(x - 1), float(y - 1));
                vec2 outside = max(max(firstCenter - pixel, pixel - firstCenter - vec2_splat(1.0)), vec2_splat(0.0));
                if (max(outside.x, outside.y) > 1.0) continue;
                float d00 = storedDepths[y * 4 + x];
                float d10 = storedDepths[y * 4 + x + 1];
                float d01 = storedDepths[(y + 1) * 4 + x];
                float d11 = storedDepths[(y + 1) * 4 + x + 1];
                if (min(min(d00, d10), min(d01, d11)) <= 0.0 ||
                    max(max(d00, d10), max(d01, d11)) >= 1.0) continue;
                float reference00 = depth + dot(depthGradient, firstCenter - pixel);
                // Every supporting sample must be a real nearer blocker.
                // A numerically displaced receiver or a clear texel cannot
                // establish a plane or fill a gap in its coverage.
                if (reference00 <= d00 + comparisonTolerance ||
                    reference00 + depthGradient.x <= d10 + comparisonTolerance ||
                    reference00 + depthGradient.y <= d01 + comparisonTolerance ||
                    reference00 + depthGradient.x + depthGradient.y <= d11 + comparisonTolerance) continue;
                float planeTolerance = 4.0 * comparisonTolerance;
                if (abs((d00 + d11) - (d10 + d01)) > planeTolerance) continue;
                vec2 blockerGradient = vec2(d10 - d00, d01 - d00);
                vec2 differenceGradient = depthGradient - blockerGradient;
                float differenceSquared = dot(differenceGradient, differenceGradient);
                if (differenceSquared <= planeTolerance * planeTolerance) continue;
                float candidateDepth = d00 + dot(blockerGradient, pixel - firstCenter);
                if (candidateDepth <= 0.0 || candidateDepth >= 1.0) continue;
                // The difference of the two depth planes is affine. Its value
                // divided by its gradient is the exact distance in shadow
                // pixels to their intersection, including grazing +/-Y faces.
                float contact = abs(depth - candidateDepth) * inversesqrt(differenceSquared);
                if (contact >= 4.0) continue;
                float extrapolation = dot(outside, outside);
                if (reconstructed && (extrapolation > bestExtrapolation ||
                    (extrapolation == bestExtrapolation && candidateDepth >= reconstructedDepth))) continue;
                bool contradictsReceiver = false;
                for (int sampleY = 0; sampleY < 4; ++sampleY)
                {
                    for (int sampleX = 0; sampleX < 4; ++sampleX)
                    {
                        float observed = storedDepths[sampleY * 4 + sampleX];
                        if (observed <= 0.0 || observed >= 1.0) continue;
                        vec2 sampleCenter = lower + vec2(float(sampleX - 1), float(sampleY - 1));
                        float biasedReference = depth + dot(depthGradient, sampleCenter - pixel);
                        // Match the actual receiver, before authored offsets.
                        // Perspective depth is c-k*(N.ray)/planeDistance, so
                        // this ratio removes the normal/radial plane offset.
                        float originalReference = perspectiveConstant +
                            (biasedReference - perspectiveConstant) * originalReferenceRatio;
                        float predicted = d00 + dot(blockerGradient, sampleCenter - firstCenter);
                        if (abs(originalReference - observed) <= comparisonTolerance &&
                            predicted < observed - comparisonTolerance)
                            contradictsReceiver = true;
                    }
                }
                if (contradictsReceiver) continue;
                if (!reconstructed || extrapolation < bestExtrapolation ||
                    (extrapolation == bestExtrapolation && candidateDepth < reconstructedDepth))
                {
                    reconstructed = true;
                    bestExtrapolation = extrapolation;
                    reconstructedDepth = candidateDepth;
                    reconstructedContact = contact;
                }
            }
        }
        if (reconstructed)
        {
            float reconstructedVisibility = depth <= reconstructedDepth + comparisonTolerance ? 1.0 : 0.0;
            // Coverage completion may recover a missed blocker. Brightening
            // still requires independent receiver evidence: a fitted plane
            // alone must not enlarge an authored bias-created separation.
            if (reconstructedVisibility < 0.5 || centralReceiver > 0.5)
                return mix(reconstructedVisibility, visibility, smoothstep(2.0, 4.0, reconstructedContact));
        }
    }
    // A fixed footprint mixes both sides of a touching join: light into the
    // blocked side and shadow into the lit side. A confirmed nearby blocker
    // tightens BOTH toward the central comparison. To brighten the lit side,
    // require actual receiver coverage: empty depth is an ambiguous raster
    // miss, not evidence of a lit surface. Full filtering returns smoothly
    // between two and four receiver texels of physical separation.
    if (centralVisibility > 0.5 && centralReceiver < 0.5) return visibility;
    return mix(centralVisibility, visibility, smoothstep(2.0, 4.0, sqrt(contactSeparationSquared)));
}

// Fixed Vogel disk, in world/light space. No screen-space or animated rotation:
// moving a receiver cannot change the sampling pattern independently of geometry.
vec2 pointShadowDisk(int index)
{
    if (index == 0) return vec2(0.176776695, 0.000000000);
    if (index == 1) return vec2(-0.225772188, 0.206825818);
    if (index == 2) return vec2(0.034558052, -0.393771179);
    if (index == 3) return vec2(0.284571220, 0.371172764);
    if (index == 4) return vec2(-0.522223187, -0.092373929);
    if (index == 5) return vec2(0.494695392, -0.314684715);
    if (index == 6) return vec2(-0.165465927, 0.615525001);
    if (index == 7) return vec2(-0.315561468, -0.607594404);
    if (index == 8) return vec2(0.684642162, 0.250030219);
    if (index == 9) return vec2(-0.712256086, 0.294008958);
    if (index == 10) return vec2(0.343354499, -0.733728620);
    if (index == 11) return vec2(0.253730241, 0.808931990);
    if (index == 12) return vec2(-0.764745892, -0.443185876);
    if (index == 13) return vec2(0.897133984, -0.197232390);
    if (index == 14) return vec2(-0.547506905, 0.778772232);
    if (index == 15) return vec2(-0.126486773, -0.976089697);
    return vec2_splat(0.0);
}

void pointShadowProject(vec3 direction, float slot, out vec2 pixel, out vec2 origin,
    out vec2 planeAxes, out float face, vec3 planeNormal)
{
    vec3 magnitude = abs(direction);
    vec2 coordinates;
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        face = direction.x >= 0.0 ? 0.0 : 1.0;
        coordinates = vec2(direction.x >= 0.0 ? direction.z : -direction.z, -direction.y) / magnitude.x;
        planeAxes = vec2(direction.x >= 0.0 ? -planeNormal.z : planeNormal.z, planeNormal.y);
    }
    else if (magnitude.y >= magnitude.z)
    {
        face = direction.y >= 0.0 ? 2.0 : 3.0;
        coordinates = vec2(-direction.x, direction.y >= 0.0 ? direction.z : -direction.z) / magnitude.y;
        planeAxes = vec2(planeNormal.x, direction.y >= 0.0 ? -planeNormal.z : planeNormal.z);
    }
    else
    {
        face = direction.z >= 0.0 ? 4.0 : 5.0;
        coordinates = vec2(direction.z >= 0.0 ? -direction.x : direction.x, -direction.y) / magnitude.z;
        planeAxes = vec2(direction.z >= 0.0 ? planeNormal.x : -planeNormal.x, planeNormal.y);
    }
    float columns = u_shadowLayout.x / 3.0;
    float blockY = floor(slot / columns);
    float blockX = slot - blockY * columns;
    float faceY = floor(face / 3.0);
    vec2 tile = vec2(blockX * 3.0 + face - faceY * 3.0, blockY * 2.0 + faceY);
    origin = tile * u_shadowAtlas.w;
    pixel = origin + vec2_splat(2.0) + (coordinates * 0.5 + 0.5) * u_shadowAtlas.z;
}

// Canonical cube rays have unit face-axis distance. Reconstruct from the actual
// sampled texel center, not the unrounded search direction, before decoding D32F.
vec3 pointShadowFaceRay(float face, vec2 coordinates)
{
    if (face < 0.5) return vec3(1.0, -coordinates.y, coordinates.x);
    if (face < 1.5) return vec3(-1.0, -coordinates.y, -coordinates.x);
    if (face < 2.5) return vec3(-coordinates.x, 1.0, coordinates.y);
    if (face < 3.5) return vec3(-coordinates.x, -1.0, -coordinates.y);
    if (face < 4.5) return vec3(-coordinates.x, -coordinates.y, 1.0);
    return vec3(coordinates.x, -coordinates.y, -1.0);
}

float pointShadowPerspectiveDepth(float axial, float nearPlane, float farPlane)
{
    return farPlane / (farPlane - nearPlane) - farPlane * nearPlane / ((farPlane - nearPlane) * axial);
}

bool tryPointShadowVisibilitySoft(int lightIndex, vec4 lightPosition, vec4 lightColor, vec3 position,
    vec3 geometricNormal, vec3 planeNormal, float sourceRadius, out float softResult)
{
    if (lightIndex < 0) { softResult = 1.0; return true; }
    vec4 settings = u_pointShadow[lightIndex];
    if (settings.x < 0.0 || u_shadowAtlas.z < 1.0) { softResult = 1.0; return true; }
    vec3 relative = position - lightPosition.xyz;
    float radial = length(relative);
    float nearPlane = settings.y;
    float farPlane = inversesqrt(max(lightColor.w, 1.0 / (100000.0 * 100000.0)));
    if (radial <= nearPlane || radial >= farPlane) { softResult = 1.0; return true; }
    vec3 L = -relative / radial;
    float axial = max(max(abs(relative.x), abs(relative.y)), abs(relative.z));
    float texelWorld = 2.0 * axial / u_shadowAtlas.z;
    float normalCosine = clamp(dot(geometricNormal, L), 0.0, 1.0);
    relative += geometricNormal * (settings.w * texelWorld * sqrt(max(0.0, 1.0 - normalCosine * normalCosine)));
    float receiverDistance = length(relative);
    float biasScale = max(0.0, 1.0 - settings.z / max(receiverDistance, 0.000001));
    float planeDistance = dot(planeNormal, relative) * biasScale;
    if (abs(planeDistance) <= max(receiverDistance * 0.000001, 0.000001))
        return false;

    vec3 direction = relative / max(receiverDistance, 0.000001);
    // Anchor the disk to the fixed receiving triangle, rather than switching
    // an axis when the moving light ray crosses a pole. Projecting that basis
    // is continuous except at grazing incidence, where the PCF fallback applies.
    vec3 receiverNormal = normalize(planeNormal);
    // The readable prepass orients normals toward its camera; derivative
    // fragment normals can have the opposite sign. Pick the same hemisphere
    // for disk orientation so analytical and opaque visibility use one kernel.
    vec3 normalMagnitude = abs(receiverNormal);
    float normalAxis = normalMagnitude.x >= normalMagnitude.y && normalMagnitude.x >= normalMagnitude.z ? receiverNormal.x :
        normalMagnitude.y >= normalMagnitude.z ? receiverNormal.y : receiverNormal.z;
    if (normalAxis < 0.0) receiverNormal = -receiverNormal;
    vec3 axis = abs(receiverNormal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 fixedTangent = normalize(cross(axis, receiverNormal));
    vec3 tangent = fixedTangent - direction * dot(fixedTangent, direction);
    float tangentSquared = dot(tangent, tangent);
    if (tangentSquared <= 0.00000001)
        return false;
    tangent *= inversesqrt(tangentSquared);
    vec3 bitangent = cross(direction, tangent);
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    LOOP for (int sampleIndex = 0; sampleIndex < 12; ++sampleIndex)
    {
        // Include the central ray so small hard-umbra blockers are retained.
        // Rescale eleven of the fixed disk points to fill the search disk.
        vec2 disk = sampleIndex == 0 ? vec2_splat(0.0) : pointShadowDisk(sampleIndex - 1) * 1.206045378;
        vec3 query = relative + (tangent * disk.x + bitangent * disk.y) * sourceRadius;
        vec2 pixel, origin, planeAxes;
        float face;
        pointShadowProject(query, settings.x, pixel, origin, planeAxes, face, planeNormal);
        vec2 texel = clamp(floor(pixel) + vec2_splat(0.5), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
        vec2 coordinates = ((texel - origin - vec2_splat(2.0)) / u_shadowAtlas.z) * 2.0 - 1.0;
        vec3 ray = pointShadowFaceRay(face, coordinates);
        float denominator = dot(planeNormal, ray);
        if (abs(denominator) <= 0.000001) continue;
        float receiverAxial = planeDistance / denominator;
        if (receiverAxial <= nearPlane || receiverAxial >= farPlane) continue;
        float receiverDepth = pointShadowPerspectiveDepth(receiverAxial, nearPlane, farPlane);
        // Native D3D11 raw load of the already-bound D32F texture. Comparison
        // taps below reuse its existing sampler; no new texture slot is needed.
        float blockerDepth = clamp(s_pointShadow.m_texture.Load(ivec3(ivec2(texel), 0)).x, 0.0, 1.0);
        if (blockerDepth >= 1.0 || blockerDepth >= receiverDepth) continue;
        float blockerAxial = nearPlane * farPlane / max(farPlane - blockerDepth * (farPlane - nearPlane), nearPlane);
        float blockerDistance = max(blockerAxial * length(ray), nearPlane);
        blockerSum += blockerDistance;
        blockerCount += 1.0;
    }
    if (blockerCount <= 0.0) { softResult = 1.0; return true; }
    float blockerDistance = max(blockerSum / blockerCount, nearPlane);
    float penumbra = sourceRadius * max(receiverDistance - blockerDistance, 0.0) / blockerDistance;
    // Contact and sub-texel penumbras use the existing stable comparison filter.
    if (penumbra <= texelWorld)
        return false;

    float visibility = 0.0;
    LOOP for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)
    {
        vec2 disk = pointShadowDisk(sampleIndex);
        vec3 query = relative + (tangent * disk.x + bitangent * disk.y) * penumbra;
        vec2 pixel, origin, planeAxes;
        float face;
        // Every direction chooses its own cube face before atlas addressing.
        // Only a hardware bilinear footprint uses that face's existing guards;
        // a wide penumbra is never clamped across point/Directional/Key tiles.
        pointShadowProject(query, settings.x, pixel, origin, planeAxes, face, planeNormal);
        vec2 coordinates = ((pixel - origin - vec2_splat(2.0)) / u_shadowAtlas.z) * 2.0 - 1.0;
        vec3 ray = pointShadowFaceRay(face, coordinates);
        float denominator = dot(planeNormal, ray);
        if (abs(denominator) <= 0.000001)
        {
            visibility += 1.0;
            continue;
        }
        float receiverAxial = planeDistance / denominator;
        if (receiverAxial <= nearPlane || receiverAxial >= farPlane)
        {
            visibility += 1.0;
            continue;
        }
        float depth = pointShadowPerspectiveDepth(receiverAxial, nearPlane, farPlane);
        vec2 gradient = planeAxes * (2.0 * farPlane * nearPlane /
            ((farPlane - nearPlane) * u_shadowAtlas.z * planeDistance));
        vec2 lowerCenter = floor(pixel - vec2_splat(0.5)) + vec2_splat(0.5);
        vec2 shallowCenter = lowerCenter + vec2(gradient.x < 0.0 ? 1.0 : 0.0, gradient.y < 0.0 ? 1.0 : 0.0);
        float sampleDepth = depth + dot(gradient, shallowCenter - pixel);
        visibility += shadow2D(s_pointShadow, vec3(pixel * u_shadowAtlas.xy, sampleDepth));
    }
    softResult = visibility / 16.0;
    return true;
}

float pointShadowVisibility(int lightIndex, vec4 lightPosition, vec4 lightColor, vec3 position,
    vec3 geometricNormal, vec3 planeNormal, float sourceRadius)
{
    // Keep one PCF call site: SM5 inlines this substantial contact filter at
    // every call, including each soft-shadow fallback. The Character Key
    // retains its separate projection and filter.
    BRANCH if (!(sourceRadius <= 0.0))
    {
        float softVisibility;
        if (tryPointShadowVisibilitySoft(lightIndex, lightPosition, lightColor, position,
            geometricNormal, planeNormal, sourceRadius, softVisibility))
            return softVisibility;
    }
    return pointShadowVisibilityPcf(lightIndex, lightPosition, lightColor, position, geometricNormal, planeNormal);
}
