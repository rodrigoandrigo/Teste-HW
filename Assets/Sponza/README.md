# Sponza scene data

`scene.spz` is generated from the AMD FSR SDK 2.3.0 official media bundle's
`cauldronmedia/SponzaNew/MainSponza.gltf` by `Samples/tools/ConvertSponza.py`.
The original scene was created by Frank Meinl and collaborators. The original
`credits_license.txt` is included in the package; personal/educational-use
restrictions apply. This scene is not covered by the SDK code's MIT license.

Static world transforms are baked into geometry. All triangles are retained.
Base-color textures are converted to RGBA8, capped at 512 pixels, with mipmaps.
The custom SPZ1 format stores counts, camera, vertices, indices, draws,
materials, and texture mip data in little-endian order. Runtime loads do not
need a glTF parser, image decoder, Python, or the desktop Cauldron host.

The renderer intentionally uses simplified lighting and alpha discard.
It does not reproduce Cauldron's complete PBR/shadow/IBL/particle pipeline.
