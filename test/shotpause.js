// capture the pause/how-to overlay (opens f950, b closes f1700)
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(1100);
var s = ares.screenshot();
s.save(ares.args[1]);
console.log("pause shot ok");
