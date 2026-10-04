// Boot-burst frame for particle screenshot
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(8);
var s = ares.screenshot();
s.save("/home/lex/n64/weedfarmer64/shots/watering.png");
console.log("burst ok");
