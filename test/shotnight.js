// Night-mood frame from the stable build
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(2546);
var s = ares.screenshot();
s.save("/home/lex/n64/weedfarmer64/shots/night.png");
console.log("night ok");
