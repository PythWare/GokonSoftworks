# The future of GokonSoftworks, supporting Team Ninja and Gust games for modding as well as RDB/fdata formats

More games are planned to be supported. For example, Dead or Alive games ate planned for modding support (specifically DOA 5 and older games since Team Ninja is still working on DOA 6). I've looked into the formats Team Ninja and Gust uses. Regarding Gust, Atelier games are planned for modding support in GokonSoftworks. I want GokonSoftworks to be the true foundation of all Koei Tecmo PC games for modding.

Also, the next update of GokonSoftworks will resolve an issue I was informed of. Some subcontainers have evaded detection for DW8E during the unpacking phase, meaning some subs are not unpacked and retain the .bin extension. So, the next update will add the variations of the signatureless subcontainers I missed. Do be aware that that will make DW8E unpack with even more files than it currently does.

Further, I may fully convert GokonSoftworks to C. Atm the majority of the toolkit is written in C, with some Python. I can recreate the GUI in C. The real benefit of GokonSoftworks going full C is that it removes Python and Pillow as requirements to use the toolkit, you'd pretty much just download a standalone executable if I go full C, 0 dependencies while still retaining my rad GUI designs.

Lastly, the RDB and fdata formats newer Koei Tecmo games use will be supported by GokonSoftworks. Due to the scale of that, it will take a few montha to fully complete GokonSoftworks' new overhaul since RDB and fdata are vastly different from LINKDATA v1 and v2.

# Version 3.0 info

Version 3.0 is very different from previous versions. it's something i've worked on separately for months (mainly rewriting the heavy logic in C, that was time consuming). Aldnoah Engine is now GokonSoftworks. It has new code (Python and C), new GUI, new mod manager, etc. Scroll to release notes for more details and for GUI example images of the toolkit

Modders that use GokonSoftworks, make sure to read the Compatible Mods section of this readme as it briefly explains what to tell your users which version of GokonSoftworks they'll need for the Mod Manager

# GokonSoftworks

Formerly Aldnoah Engine, GokonSoftworks is a Koei Tecmo  modding toolkit for Omega Force games (soon Team Ninja and Gust games as well)

GokonSoftworks is meant to be the foundation for modding the Koei Tecmo games it supports. It can unpack game containers, decompress assets, preserve rebuild metadata, rebuild subcontainers, create mod files, apply mods, disable mods, merge mods, encrypt/decrypt files that rely on encryption, etc. GokonSoftworks also has a rad design, it's not enough for software to be useful, i want using GokonSoftworks to feel like an experience as well. More tools will be made for GokonSoftworks as time goes on.

You don't need games unpacked if your only goal is to apply/disable mods (you just need to click the Generate Taildata JSON button one time), game unpacking is an optional feature for those who want to mod the files.

I HIGHLY recommend reading this readme, GokonSoftworks_Guide.txt (detailed guide on GokonSoftworks usage since the readme is getting a little long), and Aldnoah_Installer_Rules_Guide.txt (if you intend to make Aldnoah installer mods with Aldnoah Engine 2.025 or older versions). the C_Source folder is the source files for gokonsoftworks.exe to show the exe is safe, that executable is to be used by the toolkit, not you. Python owns the GUI while C handles the heavy logic, you as the end user only use main.pyw to use the toolkit

GokonSoftworks is inspired by my favorite anime "How I Attended An All-Guy's Mixer", the former toolkit Aldnoah Engine was inspired by Aldnoah.Zero

# Requirements

## Required

- Windows PC. GokonSoftworks isn't supported on Linux/Mac.
- Python 3.
- Pillow.

Install Pillow with:

```
python -m pip install pillow
```

# How to Launch

Launch the GUI with:

```text
main.pyw
```

You can double click `main.pyw` or run it from command prompt.

If GokonSoftworks doesn't launch it's usually caused by Python installed incorrectly, Pillow missing, or .pyw file associations using the wrong Python. Please verify your Python installation before reporting an GokonSoftworks bug.

Back up your game files before using GokonSoftworks.

# Supported Games

Currently supported PC games:
- Samurai Warriors 2
- Dynasty Warriors 4 Hyper
- Dynasty Warriors 6
- Dynasty Warriors 7 XL
- Dynasty Warriors 8 XL
- Dynasty Warriors 8 Empires
- Dynasty Warriors 9
- Warriors Orochi 1
- Warriors Orochi 3
- Warriors Orochi 4
- Bladestorm Nightmare
- Warriors All Stars
- Dragon Quest Builders 2

Currently supported PS3 games (MUST be decrypted before use with GokonSoftworks):
- Dynasty Warriors 6 Empires

# Release Notes of GokonSoftworks 3.0

Aldnoah Engine is changed to GokonSoftworks, this is more than a name change. Most of the toolkit has had major code changes, GUI redesign, etc. Even the Constellation Mod Manager is changed to a new Mod Manager that's eaiser to use (some gamers told me Constellation Manager was too complex). Unpacking games is way faster than before now that I rewrote the bulk of the file handling logic in C (I manage my own damn pointers).

The other changes in 3.0 are games that have intact filenames (check Intact Filenames section for more info) will now unpack with the original filenames instead of entry_number.bin when feasible, the Mod Manager is heavily optimized (fully capable of handling thousands of mods), a single mod format (.Gokon), etc

Oh and new games are added as supported (Dynasty Warriors 4 Hyper, Dynasty Warriors 6, Warriors Orochi 1, Samurai Warriors 2, Dynasty Warriors 6 Empires)

# Main Hub

The Main Hub of GokonSoftworks, I suggest running Diagnostics if it's your first time using GokonSoftworks. It essentially verifies if the current directory GokonSoftworks is located in is good for usage. It may create a tiny temp file to verify write permissions but it'll be automatically deleted since its only purpose is to make sure GokonSoftworks has write permissions in the directory it's in. Write permissions is important since that's needed for unpacking, the modding software, etc.

Click the dots in the Menu to navigate the supported games. When a game unpacks the Mocktail glass will fill based on the unpack progress, when it says "poured" the game is done unpacking.

<img width="1120" height="922" alt="1" src="https://github.com/user-attachments/assets/d56129b7-be98-4fcc-9644-841d75d36f15" />

<img width="1120" height="926" alt="2" src="https://github.com/user-attachments/assets/eec4d7f9-e488-4606-bb27-873062cc833f" />

<img width="1122" height="926" alt="3" src="https://github.com/user-attachments/assets/5a5f7072-24b4-4dbd-9ec7-5a4045766070" />

<img width="1121" height="924" alt="4" src="https://github.com/user-attachments/assets/9f2f7fe4-31f1-44af-8dbe-a33b5ea0c9dc" />

# Mod Creator

The Mod Creator turns modded files into a .gokon file for use with the Mod Manager.

<img width="782" height="889" alt="6" src="https://github.com/user-attachments/assets/da9cd043-6dd8-4844-8043-19534db3524a" />

## Package Mod

.Gokon, a custom mod format I designed. Multiple file payloads packed into one mod release.

Recommended package workflow:

1. Create a clean folder for the mod package that matches the folder layout of the original files (i.e., SW2_Unpacked\BNS\sw2_us\etc\unitbase.bin as an example of a modded unitbase.bin).
2. Place only the final modded files in that folder.
3. Don't include unnecessary subdirectories unless the tool specifically expects them.

Mod Creator can include metadata such as:

- mod name,
- author,
- version,
- description,
- preview images,
- theme audio.

# Mod Manager

GokonSoftworks includes its own high end mod manager, a one of a kind mod manager built specifically for Koei Tecmo/Omega Force container based games.

Mods are visualized as wine bottles on a dynamically growing shelf that scales based on the mods the user has. Wine bottles (mods) that aren't enabled are empty, enabled mods are filled based on the color selected by the modder that made the mod in the Mod Creator. When the wines (mods) are enabled, sparkles animate around the enabled mods to make them look pretty. In essence, the Mod Manager is far more than useful, it's a visual experience. You may bask in its radiance.

It understands GokonSoftworks's taildata system. Mods can be applied without rebuilding massive game containers (sometimes over 70 gigabytes when fully unpacked), making mod applying/disabling extremely quick.

## What makes it different

- **Container-aware modding**, applies mods directly to Koei Tecmo container/IDX structures.
- **No same-size requirement**, replacement files can be larger/smaller than the originals.
- **No forced recompression**, GokonSoftworks can apply decompressed replacement payloads when the game accepts them.
- **Safe disable support**, original IDX entries are saved in a ledger and restored when disabling mods.
- **Disable All support**, restores tracked IDX entries and truncates containers back to their original sizes.
- **Metadata-rich mods**, supports mod name, author, version, description, preview images, genre, and theme audio.
- **Mod Collision Detection**, detects mod collisions.
- **Visual mod library**, mods are visualized as wine bottles on shelves, the shelf dynamically grows as the mod count grows.
- **Virtualization**, mods/descriptions are virtualized. Meaning you can have thousands of mods, book-length descriptions for mods and the Mod Manager will handle it easily. It's a highly optimized Mod Manager.

It's designed to be unique, original, and defying the norms/expectations of mod managers. It doesn't simply overwrite files. It appends modded payloads to the correct container, updates the IDX entry, records the original state, and gives the user a way back.

# Mod Manager GUI

Use the mousewheel to scroll down as your mod count grows, click mods to preview and choose to enable/disable, etc. The Mod Manager can disable individual mods (Empty) or disable all (Empty Every Bottle) mods. Empty Every Bottle truncates containers back to their original sizes, making them vanilla while disabling all applied mods.

<img width="1243" height="829" alt="7" src="https://github.com/user-attachments/assets/89dcffe1-d978-47c2-9700-3a515a7a5833" />

<img width="1242" height="823" alt="8" src="https://github.com/user-attachments/assets/ee4232f8-ff2d-4f5d-acbd-49e408093881" />

<img width="1239" height="829" alt="9" src="https://github.com/user-attachments/assets/d1c34b9b-ff76-42fa-a311-603edb4819a4" />

## Mod Collision detection

The Mod Manager can detect mod collisions, when it does it'll notify the user.

# Intact Filenames

Some games made by Omega Force retain the original filenames but not all of them. Most games by them strip, hash, or use indexing for filenames. Also, some games that retain intact filenames are merely leftovers while the released game was switched to use hashed or indexed filenames instead of useful filenames (which means recovering the original order of the filenames is hard since then nothing in the executable references said filenames).

Games that retain most or all of the original filenames:

Samurai Warriors 2, Dynasty Warriors 4 Hyper, Dynasty Warriors 6, Warriors Orochi 1, Bladestorm Nightmare.

Games that retain most or all of the original filenames but the order of the filenames is not fully solved yet:

Dynasty Warriors 6 Empires and Dynasty Warriors 7 XL. I have figured out a large amount of the ordering for DW6E but I don't have the time to sift through 2k out of the 5k files to figure out which ones are paired correctly or not. So I included dw6e_filenames.txt which holds all of the filenames DW6E retained in its elf. If you manage to identify which files belong to what based on that list, let me know and I'll update GokonSoftworks with the corrected list (you will be credited naturally). As for DW7XL, I haven't had time to look into the ordering of the filenames but I included dw7xl_filenames.txt incase a modder looks into and wants to support GokonSoftworks (again, you'll be credited).

Games that don't retain most or any of the original filenames on PC:

Dynasty Warriors 8 XL, Dynasty Warriors 8 Empires, Dynasty Warriors 9, Warriors All Stars, Warriors Orochi 3, Warriors Orochi 4, Dragon Quest Builders 2

# Compatible Mods

GokonSoftworks only supports .gokon mods, which is the new mod format that GokonSoftworks 3.0 onward will create. If you download a mod made in older versions, use Aldnoah Engine 2.025 in the releases. If you're a modder, just remake the mods in Mod Creator so it builds as .gokon.

GokonSoftworks 2.025 supports v2, v3, and v4 for mod formats. If you're not sure what mod format your single, package, or installer mod was made in just open up the Mod Manager, select the mod, and look at what is displayed beside `Format:`

2.025 uses the v4 format but 2.025 is designed to be backwards compatible with older mod formats (v2/v3, which GokonSoftworks versions 2.023 and 2.0243 used). So if you're making mods with GokonSoftworks 2.023 or 2.0243, just tell your users to download the latest version of GokonSoftworks since they need the Mod Manager for applying the mods.

# Rebuild Containers Info

Some games have rebuild only supported containers because said containers have no offset field to repoint, that's just how Omega Force designed some containers. Either the table stores lengths and each entry follows the one before it (DW6 and DW6E) or there's no table and entries are found by scanning (BGM and voice containers). This means if you mod files from a container not supported by the Mod Manager (the files will be listed  below), you just need to use the Rebuild Containers button. Again, this isn't my fault it's just how some of the containers Omega Force designed work which makes it more optimal to rebuild the container than to force the Mod Manager to handle rebuilding.

The list of games and files that need to use the Rebuild Containers button:

Dynasty Warriors 6:

LINKDATA_UK.BIN and LINKDATA_UK.IDX

Dynasty Warriors 6 Empires:

LINKDATA.BIN and LINKDATA.IDX

Dynasty Warriors 4 Hyper 3 of its 4 containers (mdata.bin uses the offset field for linkdata.bin so it's supproted by the mod manager):

media\data\etc\resource.bin
media\data\sound\voice\voice_jp.bns
media\data\sound\voice\voice_us.bns

Samurai Warriors 2 3 of its 5 containers (the other 2 are supported by the mod manager since the offset field is used by Omega Force):

linkdata\LINKDATA_ANS_NA.lnk
linkdata\LINK_BGM.lnk
linkdata\LINK_SEBANK_NA.lnk + LINK_SEBANK_NA.idx

Warriors Orochi 2 of its 7 containers (the other 5 are supported by the mod manager since the offset field is used by Omega Force):

data\LINK_BGM.BDX
data\LINK_SEBANK.BDX + LINK_SEBANK.HDX

# Aldnoah Merger (only in Aldnoah Engine 2.025, GokonSoftworks 3.02 will include its own deluxe Mod Merger)

As explained above, this handles merging of mods. Suppose mod 1 and mod 2 edit the same files, without merging, the last applied mod would overwrite the other mod. Now, you can use Aldnoah Merger to merge mods that mod the same files. This allows modders to merge mods that contain the same files in them.

<img width="1920" height="1033" alt="newGokonSoftworks2" src="https://github.com/user-attachments/assets/d09368a8-5b4d-4686-8d45-9cf7efe9ba2b" />

<img width="1916" height="1032" alt="newGokonSoftworks3" src="https://github.com/user-attachments/assets/9db513ba-ba65-427a-8cbf-c6563c39b022" />

# Important Concept, GokonSoftworks Taildata

When GokonSoftworks unpacks files from the main game containers, it appends data to an external json named after the game GokonSoftworks was used for to unpack. Don't delete the json unless you know what you're doing.

The Mod Manager uses json/taildata to know:

- which IDX file/entry belongs to the extracted file,
- which container should receive the modded payload,
- where to patch the game to load the replacement,
- how to safely disable or restore mods later.

Taildata doesn't interfere with normal modding. You can still edit files as usual.

# Subcontainers

Many Omega Force games use subcontainers. Some have obvious signatures while many are signatureless and can only be recognized through structure.

GokonSoftworks attempts to deeply unpack these so modders can reach the actual files inside.

Subcontainers may contain:

- regular files,
- KVS audio chunks,
- model bundles,
- shader bundles,
- split-zlib wrapped resources,
- nested subcontainers,
- empty placeholder slots used for indexing.

A full unpack can produce a very large number of files, that's normal. GokonSoftworks is trying to preserve the structure needed for lossless rebuilding.

## Nested Subcontainers

Some subcontainers contain more subcontainers. GokonSoftworks creates same name folders for these.

Example:

```text
entry_00280.bin
entry_00280/
  005.KSHL
  005/
    000.vsh
    001.vsh
    002.psh
```

When rebuilding, GokonSoftworks works bottom up:

```text
rebuild inner folders first
insert rebuilt inner files into parent
rebuild parent offsets/sizes
```

You don't need to manually select every inner folder. Select the parent subcontainer folder and its original/base file. GokonSoftworks will rebuild known nested child formats automatically when the matching child folder exists.

## Empty files

Don't delete empty files created during subcontainer unpacking.

Some empty files represent real index slots. Removing them can shift file order and break rebuilding.

## Extra files warning

When rebuilding a subcontainer, don't leave unrelated extra files inside the folder being rebuilt.

If a subcontainer originally has 100 payload slots, the rebuild folder should contain the 100 files that belong to those slots. Extra files can cause file count mismatches or incorrect rebuilds.

# Rebuilding Subcontainers

To rebuild a subcontainer:

1. Find the subcontainer folder created by GokonSoftworks.
2. Replace/edit files inside that folder as needed.
3. Keep the original filenames unless you know the format allows otherwise.
4. Use GokonSoftworks's subcontainer rebuild option.
5. Select the subcontainer folder.
6. Select the original/base subcontainer file.
7. GokonSoftworks rebuilds the subcontainer.

For nested formats, GokonSoftworks can rebuild supported child containers before rebuilding the parent.

# Logs/Warnings

## `comp_log.txt`

You may see messages like:

```text
zlib decompress failed at IDX entry ... wrote raw to entry_XXXXX.bin
```

This usually means Omega Force marked a file as compressed in the IDX but the file data was not actually compressed in the expected way.

That isn't an GokonSoftworks error, it may be a mistake that happened during the game's development process.

# Performance Notes

Some games unpack very large numbers of files. That's normal especially when deep subcontainer unpacking is enabled.

Unpacking can take several minutes or longer depending on:

- game size,
- number of container entries,
- compression,
- nested subcontainer depth,
- SSD vs HDD

If the mocktail appears stuck, it isn't. It may still be working through heavy subcontainer/decompression logic.

For best results, unpack to a SSD.

# Current known limitations

- Extremely deep unpacking can produce hundreds of thousands of files.

# Credit

Credit goes to Kanbei and Zebuta for allowing me to include their txt file documentation on names and values for Warriors Orochi 3 and Bladestorm Nightmare, Credit also goes to The Tempest who spent time helping me identify maps based on their models. More Credit also goes to TwistZero for their documentation on Dynasty Warriors 8 Packs and Manny for gifting me Warriors Orochi 4 as well as his info on WO4's unit data.

Credit also goes to default.kramer for gifting me Dragon Quest Builders 2, without their contribution I probably wouldn't have looked into supporting DQB2.

Credit goes to sapphire and playinful for informing me of DQB2 using encryption, their sample files helped me solve the encryption used for DQB2

# Extra Notes

If you encounter issues or have questions contact me through GitHub, Reddit, or Discord but please make sure you read the readme, GokonSoftworks_Guide.txt, and Aldnoah_Installer_Rules_Guide.txt first since those answer a lot of questions already.

If Koei Tecmo has any issue with GokonSoftworks/Aldnoah Engine, please contact me so I can comply. GokonSoftworks is intended for modding offline games so players and modders can keep enjoying them long after official support ends.

GokonSoftworks includes 2 PNG images in the pngs folder (bottle.png and glass.png), I don't own them. bottle.png and glass.png are free images that had a free distribution license that I downloaded to use with the GUI but their license forbids monetizing them. So if you use GokonSoftworks, you are now informed those assets are not permitted for financial gain.

## Recommended/Optional Tools

### Noesis/Project G1M

Noesis and Joschuka's Project G1M scripts are recommended for viewing/converting many G1M/G1T files:

https://github.com/Joschuka/Project-G1M

G1M/G1T formats vary across Koei Tecmo games so porting files between games may require extra work.

### eArmada8 Gust Tools

eArmada8 made tools for Gust game formats that can also be useful for some Koei Tecmo assets:

https://github.com/eArmada8/gust_stuff

### Kybernes Tools

Kybernes Tools is recommended alongside GokonSoftworks for extra modding workflows, scanning tools, Editors, and audio related tools.

https://github.com/PythWare/Kybernes-Tools

For audio modding, Harklight from Kybernes Tools may be needed for replacing or creating audio such as voices, sounds, music, etc.

### Kybernes Batch Binary File Scanner

For searching through large unpacked folders, use Batch Binary File Scanner:

https://github.com/PythWare/Kybernes-Batch-Binary-File-Scanner

GokonSoftworks extracts many files with generated names because many later Koei Tecmo games strip, hash, or use indexing for filenames. A binary scanner makes research/modding much easier.
