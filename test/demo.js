// Full gameplay demo: every presented frame to PNG + game audio to WAV
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.startAudio();
ares.waitFrames(60);   // boot: assets load before VI comes up
var n = 0;
var total = Number(ares.args[1] || 2500);
while (n < total) {
  ares.waitFrames(2);
  var s = ares.screenshot();
  s.save("/home/lex/.hermes/cache/scratch/frames/f" + ("00000" + n).slice(-5) + ".png");
  n = n + 2;
}
var a = ares.stopAudio();
a.save("/home/lex/.hermes/cache/scratch/demo_audio.wav");
console.log("demo done frames=" + (n/2));
