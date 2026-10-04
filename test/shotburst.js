// Boot-burst water particles on title screen, front cam
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(20);
var s = ares.screenshot();
s.save("/home/lex/n64/weedfarmer64/shots/watering.png");
console.log("burst ok");
