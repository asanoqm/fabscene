import processing.serial.*;

Serial myPort;
String incoming = "";
float bellVal = 0;
float noiseVal = 0;
float woodVal = 0;

// スムーズ表示用
float dispBell = 0;
float dispNoise = 0;
float dispWood = 0;

void setup() {
  size(600, 400);
  printArray(Serial.list());
  myPort = new Serial(this, "/dev/cu.usbmodem1101", 115200);
  textAlign(CENTER, CENTER);
  textSize(16);
  smooth();
}

void draw() {
  background(30);

  // データ受信
  while (myPort.available() > 0) {
    incoming = trim(myPort.readStringUntil('\n'));
    if (incoming != null) {
      println(incoming);
      String[] parts = split(incoming, ' ');
      if (parts.length == 2) {
        String label = parts[0];
        float val = float(parts[1]);
        if (label.equals("bell")) bellVal = val;
        else if (label.equals("noise")) noiseVal = val;
        else if (label.equals("wood")) woodVal = val;
      }
    }
  }

  // 線形補間でスムーズに
  dispBell = lerp(dispBell, bellVal, 0.1);
  dispNoise = lerp(dispNoise, noiseVal, 0.1);
  dispWood = lerp(dispWood, woodVal, 0.1);

  // グラフ描画（左下から bell, noise, wood）
  drawVerticalBar("bell", dispBell, 150, color(220, 220, 220));
  drawVerticalBar("noise", dispNoise, 300, color(100, 100, 100));
  drawVerticalBar("wood", dispWood, 450, color(165, 42, 42));
}

void drawVerticalBar(String label, float value, float x, color c) {
  float h = map(value, 0, 1, 0, height - 80);  // 高さをマッピング
  fill(c);
  noStroke();
  rect(x - 40, height - h - 60, 80, h, 8);     // 下から上に伸びる
  fill(255);
  text(label + "\n" + nf(value, 0, 2), x, height - 25);  // ラベル
}
