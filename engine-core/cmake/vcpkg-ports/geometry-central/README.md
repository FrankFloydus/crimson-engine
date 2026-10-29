# Intrinsic decal chart solver

Pinned geometry-central `019669ddabda05e0f71fa3587cfb3c1dadf19cb8` (MIT), with its exact happly gitlink `88f79725d32d78e0e637ebf42d8432d519e1c7d0` and both archive SHA512 values fixed in the port. The library is static, uses the manifest baseline's Eigen, and disables SuiteSparse. The tracked patch requires the package-managed Eigen before upstream dependency resolution, so configure never fetches another Eigen. No viewer or application is built.

The public Crimson adapter owns all data types, tolerances and topology decisions. Library types do not cross `src/decals/DecalSurface.h`. `Build.cmd Release decal-proof` configures the isolated CPU proof project and never links or launches the renderer.
