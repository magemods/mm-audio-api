# API Notes

Mods should first register themselves with the API, passing the name of their mod, and receiving back a handle. This is so that we know where to look for their nrm file.

Mods can use the handle to register sequences, soundfonts, and samples either by passing a filename included in their nrm, or a raw buffer.

Mods can add instruments, drums, and sfx to soundfonts. Mods can add sfx to sequences. Mods can replace any of the MM sequences or soundfonts with their custom one.


AudioApi_Add(Sample|Soundfont|Sequence)("mod_name", "filename");
AudioApi_Add(Sample|Soundfont|Sequence)Raw(buffer);

AudioApi_AddSampleToSoundfont(




// Simplified commands for replacing a sfx in sequence 0?
AudioApi_ReplaceSeq0Sfx("mod_name", "filename", sfx_id);
AudioApi_ReplaceSeq0SfxRaw(buff, sfx_id);


Built in recomp-ui for viewing all (custom and built in) sounds with ability to play?
