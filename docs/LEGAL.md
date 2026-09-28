# Legal and Distribution Boundaries

This document is an engineering/distribution policy for MZM-Recomp, not legal advice.

## 1. Core rule

The public repository must remain useful **without distributing Nintendo game content**.

Do not commit or publish:

- Metroid: Zero Mission ROM images;
- Nintendo GBA BIOS images;
- ROM patches that unlawfully redistribute substantial copyrighted content;
- extracted graphics, music, sound, text or other original game assets unless there is a clear lawful basis;
- generated C/C++ that is directly derived from the copyrighted ROM if redistribution rights have not been established;
- user save files containing private/local data by accident.

Users are expected to supply their own legally obtained cartridge image where required.

## 2. Hashes are allowed project metadata

Cryptographic hashes, cartridge size, game code and revision metadata are used only to identify the supported cartridge revision.

Primary USA identity:

```text
SHA-1: 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
Size:  8388608 bytes
```

Secondary Europe identity:

```text
SHA-1: 0fd107445a42e6f3a3e5ce8c865f412583179903
Size:  8388608 bytes
```

## 3. Generated code policy

GBARecomp translates ROM machine code into generated C++.

The upstream GBARecomp guidance treats generated ROM-derived source as local material rather than public framework source.

MZM-Recomp therefore follows this policy:

- generate locally from the user's verified ROM;
- keep generated output ignored/untracked;
- never hand-edit generated output as the canonical fix;
- distribute configuration, scripts and original integration code rather than generated cartridge-derived source, unless redistribution rights are later established.

## 4. Upstream licenses

### metroidret/mzm

The audited upstream is licensed under the **MIT License**.

That license applies to the upstream software/source under its terms. It does not grant rights to Nintendo's original game assets or trademarks.

### GBARecomp

The audited GBARecomp upstream is licensed under **PolyForm Noncommercial License 1.0.0**.

This matters materially to MZM-Recomp:

- noncommercial uses described by that license are permitted;
- commercial distribution/use cannot be assumed;
- any downstream packaging that contains or derives from licensed GBARecomp components must comply with its terms.

Consult the upstream license text for exact obligations.

## 5. MZM-Recomp repository license

A blanket repository license is intentionally **not selected during M0**.

Reason: the repository will combine original project documentation/scripts with integrations that may depend on upstream code under different terms. Before adding a top-level license, source ownership and dependency boundaries must be explicit.

When implementation code is added, each category should be reviewed:

- wholly original MZM-Recomp code;
- copied/modified MIT upstream code;
- copied/modified PolyForm-licensed code;
- generated ROM-derived material, which should remain local/untracked.

Do not add a top-level MIT license merely because `metroidret/mzm` is MIT; that would misrepresent GBARecomp-derived components.

## 6. ROM and BIOS acquisition

The project should never:

- host ROM/BIOS downloads;
- link to piracy-oriented ROM/BIOS downloads;
- automate downloading copyrighted ROM/BIOS images from unauthorized sources;
- bundle a cartridge image into releases.

Build instructions should explain where the user must place a legally obtained local file and how it is hash-verified.

## 7. Saves and patches

Save compatibility is part of the technical project, but public tests should use synthetic or intentionally shareable fixtures where possible.

Any future binary patch/mod distribution should be designed to avoid embedding substantial original copyrighted content.

## 8. Trademarks and affiliation

MZM-Recomp is an independent fan/research project.

It is not affiliated with, sponsored by, or endorsed by Nintendo.

Metroid, Metroid: Zero Mission, Nintendo, Game Boy Advance, and related marks/assets belong to their respective owners.

## 9. Release checklist

Before a public binary release:

- [ ] verify no ROM is embedded;
- [ ] verify no GBA BIOS is embedded;
- [ ] verify no generated ROM-derived source is accidentally packaged;
- [ ] review GBARecomp license compliance;
- [ ] include required upstream notices/licenses;
- [ ] make the supported ROM hash explicit;
- [ ] ensure the application rejects unsupported revisions safely;
- [ ] review release assets for extracted copyrighted game material;
- [ ] keep compatibility/enhancement claims evidence-based.
