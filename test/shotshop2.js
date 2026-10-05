// shop open window: sim f 400..800. VI frames ~= half sim frames. Shoot at VI 250.
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(250);
var s = ares.screenshot();
s.save(ares.args[1] || "/home/lex/n64/weedfarmer64/shots/shop6.png");
console.log("shop6 ok");
