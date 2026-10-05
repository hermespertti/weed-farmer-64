// capture walk-mode frames inside walking windows (shop trips at f200/600/...)
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(150);
var s = ares.screenshot();
s.save(ares.args[1]);
ares.waitFrames(350);   // frame ~500
s = ares.screenshot();
s.save(ares.args[2]);
ares.waitFrames(350);   // frame ~850 (b closes shop at 900)
s = ares.screenshot();
s.save(ares.args[3]);
console.log("triple shot done");
