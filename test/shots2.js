// Multi-moment capture: particle burst + night
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(206);           // right after AUTOTEST A-press = water burst
var s1 = ares.screenshot();
s1.save("/home/lex/n64/weedfarmer64/shots/watering.png");
ares.waitFrames(2340);          // frame 2546: night window (dayT ~40.8s)
var s2 = ares.screenshot();
s2.save("/home/lex/n64/weedfarmer64/shots/night.png");
console.log("multi ok");
