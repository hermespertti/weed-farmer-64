// Early-frame capture (boot hooks set the mood)
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(300);
var s = ares.screenshot();
s.save(ares.args[1]);
console.log("frame ok");
