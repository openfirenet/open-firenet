#include "firenet_link.h"
#include <iostream>
using namespace firenet;

int main(){
  std::string wire; uint32_t clk=0;
  DongleLink link([&](const uint8_t*d,size_t n){ wire.append((const char*)d,n); },
                  [&](){ return clk; });
  int ok=0,ko=0;
  auto CH=[&](const char*n,bool c){ if(c)ok++; else{ko++; std::cout<<"ECHEC "<<n<<"\n";} };
  // émission cadencée : poll empile puis émet une trame par TX_GAP_MS -> on draine
  auto drain=[&](){ for(int i=0;i<64 && !link.txIdle();i++){ clk+=DongleLink::TX_GAP_MS; link.poll(); } };
  // négociation : DOMO/V3 est la première famille sondée (zéro changement pour les utilisateurs DOMO existants)
  link.poll(); drain();
  CH("V3 version emitted first (detection default)", wire.find("GET_CDCDEVICE3_VERSION=0; ")!=std::string::npos);
  // le poêle répond FINISHED
  std::string fin="GET_CDCDEVICE_VERSION_FINISHED";
  for(char c:fin) link.onByte(c);
  clk+=60; link.poll();              // silence écoulé (>SILENCE_MS après le dernier octet)
  CH("version acquittée", link.model().version_ack && link.model().generation==1);
  CH("V3 profile locked after ACK", link.model().version_profile==DongleLink::DETECT_V3);
  // stove emits POST_CDCDEVICE_STATUS with plain text SSID (DT=1, 19 fields)
  wire.clear();
  // ssid hex-encoded: DOMO/V3 dialect (dt()==3) hex-encodes it on the wire, ASCII "MonSSID"
  std::string st="POST_CDCDEVICE_STATUS=0;\n0\n1\n0\n0\n5\n0\n0\n101\n112\n360\n0\n-52\n17800020\nfHTeLam2\n3\n4D6F6E53534944\nsecret\n192.168.1.5\nAA:BB\n";
  for(char c:st) link.onByte(c);
  clk+=60; link.poll();              // silence elapsed
  CH("hex ssid decoded back to plain text", link.model().status.at("ssid")=="MonSSID");
  CH("app_version parsed", link.model().status.at("app_version")=="112");
  // POST_SENSORS différentiel
  std::string ps="POST_SENSORS=0; temp=213; status=1; ";
  for(char c:ps) link.onByte(c);
  clk+=60; link.poll();
  CH("sensors fusionnés", link.model().sensors.at("temp")==213 && link.model().sensors.at("status")==1);
  std::string ps2="POST_SENSORS=0; temp=218; ";
  for(char c:ps2) link.onByte(c);
  clk+=60; link.poll();
  CH("diff appliqué", link.model().sensors.at("temp")==218 && link.model().sensors.at("status")==1);
  // fragmentation USB : une trame arrivant en 2 morceaux à <SILENCE_MS d'intervalle
  // doit être recomposée et traitée une seule fois.
  {
    std::string w2; uint32_t c2=0;
    DongleLink l2([&](const uint8_t*d,size_t n){ w2.append((const char*)d,n); },
                  [&](){ return c2; });
    std::string f="GET_CDCDEVICE_VERSION_FINISHED";
    for(size_t i=0;i<10;i++) l2.onByte(f[i]);
    c2+=10; l2.poll();                // < SILENCE_MS : pas de dispatch
    CH("fragment 1 en attente", !l2.model().version_ack);
    for(size_t i=10;i<f.size();i++) l2.onByte(f[i]);
    c2+=60; l2.poll();                // > SILENCE_MS après fin : dispatch
    CH("recomposé", l2.model().version_ack);
  }
  // dump complet (§8.5) : valeurs positionnelles pures ("=val" sans nom)
  // le poêle peut les fragmenter sur plusieurs trames (continuation).
  {
    DongleLink l2([](const uint8_t*,size_t){}, [&](){ return clk; });
    // trame 1 : commence par POST_SENSORS=0; puis du positionnel
    std::string d = "POST_SENSORS=0; =218; =45; =0; =0; =0; =0; ";
    for(char ch:d) l2.onByte(ch);
    clk+=50; l2.poll();
    CH("dump part 1", l2.model().sensors_pos.size()==6);
    CH("dump s00=218", l2.model().sensors_pos[0]==218);
    CH("dump s01=45",  l2.model().sensors_pos[1]==45);
    // trame 2 : continuation "=val" SANS "POST_SENSORS" en tête -> doit prolonger sensors_pos
    std::string d2 = "=0; =70; =0; =1450; ";
    for(char ch:d2) l2.onByte(ch);
    clk+=50; l2.poll();
    CH("continuation accumulée", l2.model().sensors_pos.size()==10);
    CH("continuation s09=1450", l2.model().sensors_pos[9]==1450);
  }
  // cas réel poêle : continuations multiples avec positions prouvées
  {
    DongleLink l2([](const uint8_t*,size_t){}, [&](){ return clk; });
    // trame initiale POST_SENSORS
    std::string h = "POST_SENSORS=0; =213; =47; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =1; =0; ";
    for(char ch:h) l2.onByte(ch);
    clk+=50; l2.poll();
    std::string k = "=0; =1; =1; =1; =1; =1; =1; =29; =70; =70; =70; =1; =3; =0; =0; =1; =13; =3; =229; =0; =0; =160; ";
    for(char ch:k) l2.onByte(ch);
    clk+=50; l2.poll();   // continuation
    std::string k2 = "=150; =112; =58512; =53404; =12201; =4354; =0; =7064; =700; =0; =0; ";
    for(char ch:k2) l2.onByte(ch);
    clk+=50; l2.poll();  // continuation
    CH("total 53 capteurs", l2.model().sensors_pos.size()==53);
    CH("s47 pelletHours=4354", l2.model().sensors_pos[47]==4354);
    CH("s49 pelletsTotal=7064", l2.model().sensors_pos[49]==7064);
    CH("s50 serviceCountdown=700", l2.model().sensors_pos[50]==700);
    // commande suivante clôt l'accumulation
    std::string end = "GET_CDCDEVICE_VERSION_FINISHED";
    for(char ch:end) l2.onByte(ch);
    clk+=50; l2.poll();
    std::string stray = "=999; ";
    for(char ch:stray) l2.onByte(ch);
    clk+=50; l2.poll();
    CH("continuation rejetée après clôture", l2.model().sensors_pos.size()==53);
  }
  // applyControls immediately triggers GET_CONTROLS=1 + GET_REVISION + 2x TRANSFER_COMPLETED
  {
    std::string sent; uint32_t c3=0;
    DongleLink l3([&](const uint8_t*d,size_t n){ sent.append((const char*)d,n); },
                  [&](){ return c3; });
    std::string ack = "GET_CDCDEVICE_VERSION_FINISHED";
    for (char c : ack) l3.onByte(c);
    l3.applyControls({{"onOff",1},{"mode",1},{"targetStage",80},{"roomTarget",220}});
    CH("applyControls queues 6 frames (drain + apply + refresh)", l3.txPending()==6);
    // Frames 1-2: preventive drain (§13.2)
    c3+=DongleLink::TX_GAP_MS; l3.poll();
    c3+=DongleLink::TX_GAP_MS; l3.poll();
    CH("preventive drain emitted", sent.find("TRANSFER_COMPLETED")!=std::string::npos);
    // Frame 3: GET_CONTROLS=1
    c3+=DongleLink::TX_GAP_MS; l3.poll();
    CH("frame 3 GET_CONTROLS=1", sent.find("GET_CONTROLS=1;")!=std::string::npos);
    CH("control parameters applied", sent.find("onOff=1;")!=std::string::npos && sent.find("mode=1;")!=std::string::npos);
    // Frame 4: immediate GET_REVISION
    c3+=DongleLink::TX_GAP_MS; l3.poll();
    CH("frame 4 GET_REVISION", sent.find("GET_REVISION=")!=std::string::npos);
    // Frames 5-6: post-command TRANSFER_COMPLETED
    c3+=DongleLink::TX_GAP_MS; l3.poll();
    c3+=DongleLink::TX_GAP_MS; l3.poll();
    CH("frames 5-6 TRANSFER_COMPLETED", l3.txIdle());

    // Simulate partial frame received from stove: POST_CONTROLS with only revision and roomTarget
    std::string partial = "POST_CONTROLS=0; revision=0; roomTarget=210; ";
    for (char c : partial) l3.onByte(c);
    c3 += 60; l3.poll();
    CH("partial controls merged", l3.model().controls.at("roomTarget")==210);
    CH("controls_pos maintains 5 elements", l3.model().controls_pos.size()==5);
    CH("controls_pos[4] updated", l3.model().controls_pos[4]==210);
    CH("controls_pos[1] onOff preserved", l3.model().controls_pos[1]==1);
    CH("controls_pos[2] mode preserved", l3.model().controls_pos[2]==1);

    // MultiAir control test
    sent.clear();
    l3.applyControls({{"convectionFan1Active", 1}, {"convectionFan1Level", 4}, {"convectionFan1Area", 10}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("MultiAir extended frame emitted", sent.find("convectionFan1Active=1;")!=std::string::npos);
    CH("MultiAir level emitted", sent.find("convectionFan1Level=4;")!=std::string::npos);
    CH("MultiAir area emitted", sent.find("convectionFan1Area=10;")!=std::string::npos);
    CH("controls_pos has 29 elements", l3.model().controls_pos.size()>=29);
    CH("controls_pos[23] is fan1Active", l3.model().controls_pos[23]==1);
    CH("controls_pos[24] is fan1Level", l3.model().controls_pos[24]==4);
    CH("controls_pos[25] is fan1Area", l3.model().controls_pos[25]==10);
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_REVISION
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 3
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 4

    // Partial update after MultiAir maintains extended controls_pos
    std::string partial2 = "POST_CONTROLS=0; revision=0; roomTarget=220; ";
    for (char c : partial2) l3.onByte(c);
    c3 += 60; l3.poll();
    CH("controls_pos still has 29 elements", l3.model().controls_pos.size()>=29);
    CH("MultiAir fan1Active preserved across partial POST", l3.model().controls_pos[23]==1);
    CH("MultiAir fan1Level preserved across partial POST", l3.model().controls_pos[24]==4);

    // Heating schedule control test
    sent.clear();
    l3.applyControls({{"heatingTimesActive", 1}, {"setBackTemp", 180}, {"heatTimeMon1", 8001030}, {"heatTimeThu1", 21002200}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("heatingTimesActive emitted", sent.find("heatingTimesActive=1;")!=std::string::npos);
    CH("setBackTemp emitted", sent.find("setBackTemp=180;")!=std::string::npos);
    CH("heatTimeMon1 emitted", sent.find("heatTimeMon1=8001030;")!=std::string::npos);
    CH("heatTimeThu1 emitted", sent.find("heatTimeThu1=21002200;")!=std::string::npos);
    CH("controls_pos[21] is heatingTimesActive", l3.model().controls_pos[21]==1);
    CH("controls_pos[22] is setBackTemp", l3.model().controls_pos[22]==180);
    CH("controls_pos[7] is heatTimeMon1", l3.model().controls_pos[7]==8001030);
    CH("controls_pos[13] is heatTimeThu1", l3.model().controls_pos[13]==21002200);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // Frost protection control test
    sent.clear();
    l3.applyControls({{"frostProtectionActive", 1}, {"frostProtectionTemp", 75}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("frostProtectionActive emitted", sent.find("frostProtectionActive=1;")!=std::string::npos);
    CH("frostProtectionTemp emitted", sent.find("frostProtectionTemp=75;")!=std::string::npos);
    CH("controls_pos has at least 31 elements", l3.model().controls_pos.size()>=31);
    CH("controls_pos[29] is frostProtectionActive", l3.model().controls_pos[29]==1);
    CH("controls_pos[30] is frostProtectionTemp", l3.model().controls_pos[30]==75);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // Frost protection scaling / clamping test
    sent.clear();
    l3.applyControls({{"frost_protection_active", 0}, {"frost_protection_temperature", 5}}); // 5 -> 50
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("frostProtectionActive=0 emitted", sent.find("frostProtectionActive=0;")!=std::string::npos);
    CH("frostProtectionTemp=50 emitted", sent.find("frostProtectionTemp=50;")!=std::string::npos);
    CH("controls_pos[29] is 0", l3.model().controls_pos[29]==0);
    CH("controls_pos[30] is 50", l3.model().controls_pos[30]==50);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // DOMO BACK bakeTarget control test
    sent.clear();
    l3.applyControls({{"bakeTarget", 220}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("bakeTarget=220 emitted", sent.find("bakeTarget=220;")!=std::string::npos);
    CH("controls_pos[5] is bakeTarget 220", l3.model().controls_pos[5]==220);
    CH("model controls bakeTarget is 220", l3.model().controls.at("bakeTarget")==220);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // DOMO BACK bakeTarget clamping & alias test
    sent.clear();
    l3.applyControls({{"bake_target_temperature", 100}}); // Clamped to 130
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("bakeTarget clamped low to 130", sent.find("bakeTarget=130;")!=std::string::npos);
    CH("controls_pos[5] clamped to 130", l3.model().controls_pos[5]==130);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    sent.clear();
    l3.applyControls({{"bakeTemp", 400}}); // Clamped to 340
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("bakeTarget clamped high to 340", sent.find("bakeTarget=340;")!=std::string::npos);
    CH("controls_pos[5] clamped to 340", l3.model().controls_pos[5]==340);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // Room temperature offset control test
    sent.clear();
    l3.applyControls({{"roomTempOffset", 15}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("roomTempOffset=15 emitted", sent.find("roomTempOffset=15;")!=std::string::npos);
    CH("controls_pos has at least 32 elements", l3.model().controls_pos.size()>=32);
    CH("controls_pos[31] is roomTempOffset 15", l3.model().controls_pos[31]==15);
    CH("model controls roomTempOffset is 15", l3.model().controls.at("roomTempOffset")==15);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // Room temperature offset negative values
    sent.clear();
    l3.applyControls({{"roomTempOffset", -25}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("roomTempOffset=-25 emitted", sent.find("roomTempOffset=-25;")!=std::string::npos);
    CH("controls_pos[31] is -25", l3.model().controls_pos[31]==-25);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // Room temperature offset clamping (-40..+40)
    sent.clear();
    l3.applyControls({{"roomTempOffset", -55}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("roomTempOffset clamped low to -40", sent.find("roomTempOffset=-40;")!=std::string::npos);
    CH("controls_pos[31] clamped to -40", l3.model().controls_pos[31]==-40);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    sent.clear();
    l3.applyControls({{"roomTempOffset", 55}});
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("roomTempOffset clamped high to 40", sent.find("roomTempOffset=40;")!=std::string::npos);
    CH("controls_pos[31] clamped to 40", l3.model().controls_pos[31]==40);
    while (!l3.txIdle()) { c3 += DongleLink::TX_GAP_MS; l3.poll(); }

    // Room temperature offset alias & scaling
    sent.clear();
    l3.applyControls({{"room_temperature_offset", -2}}); // Scaled to -20
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 1
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // drain 2
    c3+=DongleLink::TX_GAP_MS; l3.poll(); // GET_CONTROLS=1
    CH("room_temperature_offset scaled to -20", sent.find("roomTempOffset=-20;")!=std::string::npos);
    CH("controls_pos[31] is -20", l3.model().controls_pos[31]==-20);
  }
  // Délai d'émission réglable à chaud (diagnostic) : pris en compte immédiatement par la file
  {
    std::string w; uint32_t c=1000;
    DongleLink l([&](const uint8_t*d,size_t n){ w.append((const char*)d,n); }, [&](){ return c; });
    CH("tx gap default", l.txGapMs() == DongleLink::TX_GAP_MS);
    l.setTxGapMs(0);    CH("tx gap clamped low", l.txGapMs() == 50);
    l.setTxGapMs(99999); CH("tx gap clamped high", l.txGapMs() == DongleLink::TX_GAP_MAX_MS);
    l.transferCompleted(); l.transferCompleted();
    l.setTxGapMs(1000); c += 100; l.poll();                 // 1re trame : last_tx_ms_ = 0 au départ
    size_t n1 = l.txPending();
    c += 500; l.poll();                                      // 500 ms < 1000 : rien ne part
    CH("tx gap honoured (long)", l.txPending() == n1);
    l.setTxGapMs(100); l.poll();                             // baissé à chaud : part tout de suite
    CH("tx gap lowered takes effect immediately", l.txPending() == n1 - 1);
  }
  // Post-handshake probe recovery (issue #4): a stove that keeps sending the
  // \x16 <digit> reset probe even after acknowledging the version should get
  // its handshake re-armed instead of being polled forever with dead sensors.
  {
    std::string w4; uint32_t c4=0;
    DongleLink l4([&](const uint8_t*d,size_t n){ w4.append((const char*)d,n); },
                  [&](){ return c4; });
    auto drain4=[&](){ for(int i=0;i<64 && !l4.txIdle();i++){ c4+=DongleLink::TX_GAP_MS; l4.poll(); } };
    l4.poll(); drain4();
    std::string fin4="GET_CDCDEVICE_VERSION_FINISHED";
    for(char c:fin4) l4.onByte(c);
    c4+=60; l4.poll();
    CH("probe-recovery test: ack'd first", l4.model().version_ack);
    // stove reverts to sending the bare probe digit instead of real POST_* frames
    for (int i=0;i<6;i++){ l4.onByte('0'); c4+=60; l4.poll(); }
    CH("version_ack cleared after sustained post-ack probing", !l4.model().version_ack);
    w4.clear();
    c4+=DongleLink::VERSION_RETRY_MS; l4.poll();
    CH("handshake re-sent after recovery", w4.find("GET_WIFI_VERSION")!=std::string::npos || w4.find("GET_CDCDEVICE")!=std::string::npos);
  }

  // --- Tests spécifiques Firenet V1 (INDUO V2.26 / V2.27) ---
  // 1) Handshake V1 : envoi GET_WIFI_VERSION -> réception GET_WIFI_VERSION_FINISHED -> push immédiat de GET_FIRENET_STATUS
  {
    std::string w5; uint32_t c5=0;
    DongleLink l5([&](const uint8_t*d,size_t n){ w5.append((const char*)d,n); },
                  [&](){ return c5; });
    l5.setCredentials("MonSSID", "MonPass", "192.168.1.50", "AA:BB:CC:DD:EE:FF");
    l5.debugSetStage(DongleLink::DETECT_V1);   // this block exercises V1 (INDUO 2.26/2.27) specifics only
    auto drain5=[&](){ for(int i=0;i<64 && !l5.txIdle();i++){ c5+=DongleLink::TX_GAP_MS; l5.poll(); } };
    l5.poll(); drain5();
    CH("V1 initial version emitted", w5.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; BL=101; APP=111; REV=360; DT=1; ")!=std::string::npos);
    CH("V1 version has DT=1", w5.find("DT=1; ") != std::string::npos);

    // Poêle INDUO répond GET_WIFI_VERSION_FINISHED
    w5.clear();
    std::string ack="GET_WIFI_VERSION_FINISHED\r\n";
    for(char c:ack) l5.onByte(c);
    c5+=60; l5.poll();
    CH("V1 generation == 2 after ACK", l5.model().version_ack && l5.model().generation == 2);
    drain5();
    CH("V1 immediately pushed GET_FIRENET_STATUS=0;", w5.find("GET_FIRENET_STATUS=0;\n") != std::string::npos);
    CH("V1 plain text SSID pushed", w5.find("MonSSID\n") != std::string::npos);
    CH("V1 plain text pass pushed", w5.find("MonPass\n") != std::string::npos);
    CH("V1 protocol is 1", w5.find("\n1\nMonSSID") != std::string::npos);
    {
      // Règle de validation du poêle INDUO (fn 0x8001d324) : ID = 8 chiffres, token = 8 caractères 0x21..0x7E.
      std::vector<std::string> ln; size_t p0 = w5.find("GET_FIRENET_STATUS=0;\n");
      size_t pos = p0 + std::string("GET_FIRENET_STATUS=0;\n").size();
      for (size_t q; (q = w5.find('\n', pos)) != std::string::npos && ln.size() < 19; pos = q + 1) ln.push_back(w5.substr(pos, q - pos));
      bool idOk = ln.size() >= 14 && ln[12].size() == 8, tokOk = ln.size() >= 14 && ln[13].size() == 8;
      if (idOk) for (char c : ln[12]) idOk = idOk && c >= '0' && c <= '9';
      if (tokOk) for (char c : ln[13]) tokOk = tokOk && c >= 0x21 && c <= 0x7e;
      CH("V1 status ID is exactly 8 digits (else stove raises UW27)", idOk);
      CH("V1 status token is exactly 8 printable chars", tokOk);
    }

    // Poêle INDUO répond POST_FIRENET_STATUS=0;
    std::string st_v1="POST_FIRENET_STATUS=0;\n0\n1\n0\n0\n1\n4\n0\n101\n111\n360\n0\n-55\n00000000\n00000000\n1\nMonSSID\nMonPass\n192.168.1.50\nAA:BB:CC:DD:EE:FF\n-------\n";
    for(char c:st_v1) l5.onByte(c);
    c5+=60; l5.poll();
    CH("V1 status parsed", l5.model().status.at("ssid") == "MonSSID" && l5.model().status.at("symbol") == "4");

    // 2) V1 telemetry: the names are registered once (GET_SENSORS=0; name=0; ...), the stove echoes them
    w5.clear();
    l5.pollSensors(); drain5();
    {
      size_t gs = w5.find("GET_SENSORS=0; ");
      CH("V1 pollSensors registers the names (GET_SENSORS=0; ...)", gs != std::string::npos);
      CH("V1 registration starts with the labels of V1 positions 0..4",
         w5.find("GET_SENSORS=0; roomTemp=0; flame=0; errMask32=0; errSub=0; statusWarning=0; statusService=0; augerSet=0; ") != std::string::npos);
      CH("V1 registration has V1 position 30 = mainState (DOMO 31) and 45 = appRevision (DOMO 46)",
         w5.find("stageCur=0; mainState=0; subState=0; rssi=0; ") != std::string::npos && w5.find("appRevision=0; pelletHours=0; ") != std::string::npos);
      CH("V1 registration contains onOffCycles then the official-name records from DOMO 55", w5.find("ignitionCount=0; onOffCycles=0; flameSensorOffset=0; pressureOffset=0; errCount0=0; ") != std::string::npos);
      {
        size_t end = w5.find("debug3=0; debug4=0; ");
        CH("V1 registration ends with DOMO 87 (V1 position 86, the last record the stove fills)",
           end != std::string::npos && end + 20 <= w5.size() && w5.compare(end + 20, 13, "GET_REVISION=") == 0);
      }
      size_t gr = w5.find("GET_REVISION="), t1 = w5.find("TRANSFER_COMPLETED");
      size_t t2 = t1 == std::string::npos ? t1 : w5.find("TRANSFER_COMPLETED", t1 + 1);
      CH("V1 pollSensors sends GET_REVISION after the registration", gs != std::string::npos && gr != std::string::npos && gr > gs);
      CH("V1 pollSensors sends two TRANSFER_COMPLETED after GET_REVISION", t1 != std::string::npos && t2 != std::string::npos && t1 > gr);
    }
    // registered once: the next polls must not send GET_SENSORS (it would empty the registered list)
    w5.clear();
    l5.pollSensors(); l5.pollPrio2Sensors(); drain5();
    CH("V1 second poll does not re-send GET_SENSORS", w5.find("GET_SENSORS") == std::string::npos);
    CH("V1 second poll still sends GET_REVISION and TRANSFER_COMPLETED", w5.find("GET_REVISION=") != std::string::npos && w5.find("TRANSFER_COMPLETED") != std::string::npos);

    // POST_SENSORS of the stove: named records (2.27 positions shifted by one from position 2 in the DOMO index space)
    std::string sens_v1 = "POST_SENSORS=0; roomTemp=236; flame=18; errMask32=0; mainState=1; subState=0; rssi=-41; pelletHours=4354; onOffCycles=122; ";
    for(char c:sens_v1) l5.onByte(c);
    c5+=60; l5.poll();
    CH("V1 named roomTemp", l5.model().sensors.at("roomTemp") == 236);
    CH("V1 sensors_pos[0] = roomTemp", l5.model().sensors_pos.size() > 0 && l5.model().sensors_pos[0] == 236);
    CH("V1 sensors_pos[1] = flame", l5.model().sensors_pos[1] == 18);
    CH("V1 sensors_pos[31] = mainState (DOMO index)", l5.model().sensors_pos.size() > 31 && l5.model().sensors_pos[31] == 1);
    CH("V1 sensors_pos[33] = rssi", l5.model().sensors_pos[33] == -41);
    CH("V1 sensors_pos[47] = pelletHours", l5.model().sensors_pos.size() > 47 && l5.model().sensors_pos[47] == 4354);
    CH("V1 sensors_pos[54] = onOffCycles", l5.model().sensors_pos.size() > 54 && l5.model().sensors_pos[54] == 122);
    // delta frame: only the changed record is sent, the others keep their value
    std::string sens_d = "POST_SENSORS=0; roomTemp=237; ";
    for(char c:sens_d) l5.onByte(c);
    c5+=60; l5.poll();
    CH("V1 delta frame updates roomTemp", l5.model().sensors_pos[0] == 237);
    CH("V1 delta frame keeps the other records", l5.model().sensors_pos[47] == 4354 && l5.model().sensors_pos[31] == 1);

    // 2b) V1 controls: the names are registered once (GET_CONTROLS=0; name=0; ...), the stove then posts them all and later only the changes
    w5.clear();
    l5.pollControls(); drain5();
    {
      CH("V1 pollControls registers the control names (GET_CONTROLS=0; ...)",
         w5.find("GET_CONTROLS=0; revision=0; onOff=0; mode=0; targetStage=0; roomTarget=0; ecoMode=0; heatTimeMon1=0; ") != std::string::npos);
      CH("V1 controls registration skips bakeTarget (no such record on the 2.27)", w5.find("bakeTarget") == std::string::npos);
      CH("V1 controls registration: frost / offset at V1 records 29..31, then debug0..debug4 (DOMO 33..37)",
         w5.find("frostProtectionTemp=0; roomTempOffset=0; roomSensorPower=0; debug0=0; debug1=0; debug2=0; debug3=0; debug4=0; GET_REVISION=") != std::string::npos);
      CH("V1 controls registration is followed by GET_REVISION then TRANSFER_COMPLETED",
         w5.find("debug4=0; GET_REVISION=") != std::string::npos && w5.find("TRANSFER_COMPLETED", w5.find("debug4=0; ")) != std::string::npos);
    }
    w5.clear();
    l5.pollControls(); drain5();
    CH("V1 pollControls sends nothing once registered", w5.empty());
    {
      std::string post = "POST_CONTROLS=0; revision=0; onOff=1; mode=2; targetStage=70; roomTarget=210; ecoMode=0; heatTimeMon1=360; "
                         "heatTimeMon2=1080; heatingTimesActive=1; setBackTemp=180; frostProtectionActive=1; frostProtectionTemp=50; roomTempOffset=-2; ";
      for(char c:post) l5.onByte(c);
      c5+=60; l5.poll();
      const auto& cp = l5.model().controls_pos;
      CH("V1 controls_pos[1..4] = onOff, mode, targetStage, roomTarget", cp.size() > 4 && cp[1]==1 && cp[2]==2 && cp[3]==70 && cp[4]==210);
      CH("V1 controls_pos[7] / [8] = heatTimeMon1 / heatTimeMon2 (DOMO index)", cp.size() > 8 && cp[7]==360 && cp[8]==1080);
      CH("V1 controls_pos[21] / [22] = heatingTimesActive / setBackTemp", cp.size() > 22 && cp[21]==1 && cp[22]==180);
      CH("V1 controls_pos[29] / [30] / [31] = frost active / frost temp / room offset", cp.size() > 31 && cp[29]==1 && cp[30]==50 && cp[31]==-2);
      CH("V1 controls map keyed by name", l5.model().controls.at("onOff")==1 && l5.model().controls.at("roomTempOffset")==-2);
      std::string dl = "POST_CONTROLS=0; roomTarget=220; ";
      for(char c:dl) l5.onByte(c);
      c5+=60; l5.poll();
      CH("V1 controls delta updates roomTarget and keeps the others", l5.model().controls_pos[4]==220 && l5.model().controls_pos[1]==1 && l5.model().controls_pos[22]==180);
    }
    // a command sends GET_CONTROLS=1 with five names, which replaces the registered list: register again at the next poll
    w5.clear();
    l5.applyControls({{"onOff",0}}); drain5();
    CH("V1 applyControls sends the five positional names", w5.find("GET_CONTROLS=1; revision=") != std::string::npos);
    w5.clear();
    l5.pollControls(); drain5();
    CH("V1 controls registered again after a command", w5.find("GET_CONTROLS=0; revision=") != std::string::npos);

    // 3) Réinitialisation par STX '0' ETX (\x02 0 \x03)
    w5.clear();
    l5.onByte(0x02); l5.onByte('0'); l5.onByte(0x03);
    c5+=60; l5.poll();
    CH("V1 STX 0 ETX clears version_ack immediately", !l5.model().version_ack);
    drain5();
    CH("V1 handshake re-armed after session reset", w5.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ") != std::string::npos);
    {
      // after a link reset the names are registered again on the next poll
      w5.clear();
      std::string fin2="GET_WIFI_VERSION_FINISHED";
      for(char c:fin2) l5.onByte(c);
      c5+=60; l5.poll();
      l5.pollSensors(); drain5();
      CH("V1 names registered again after a new handshake", w5.find("GET_SENSORS=0; roomTemp=0; ") != std::string::npos);
    }
  }

  // 4) Rafale de 0x16 : réponse immédiate au premier, puis plafonnée (issue #4, log du 23/09)
  {
    std::string w6; uint32_t c6=1000; int syn_lines=0;
    DongleLink l6([&](const uint8_t*d,size_t n){ w6.append((const char*)d,n); },
                  [&](){ return c6; });
    l6.debugSetStage(DongleLink::DETECT_V1);   // this block exercises the V1 SYN-burst behaviour specifically
    l6.onDebug([&](const char* dir, const std::string&){ if (std::string(dir)=="syn") syn_lines++; });
    l6.onByte(0x16); l6.poll();
    CH("SYN: first 0x16 answered immediately", w6.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ") != std::string::npos);
    for (int i=0;i<250;i++) { c6+=8; l6.onByte(0x16); l6.poll(); }   // ~2 s de 0x16 toutes les 8 ms
    size_t frames=0; for (size_t p=0; (p=w6.find("GET_WIFI_VERSION", p)) != std::string::npos; p++) frames++;
    CH("SYN: reply rate capped (<= 1 per TX_GAP_MS)", frames <= 2000/DongleLink::TX_GAP_MS + 3);
    CH("SYN: every 0x16 counted", l6.synCount() == 251);
    CH("SYN: summarised, not logged per byte", syn_lines >= 1 && syn_lines <= 4);
  }

  // --- Auto-detection of the stove family (DOMO/V3 -> INDUO II 2.28 -> INDUO V1, cyclic) ---
  // 5) Default order and per-stage content
  {
    std::string w7; uint32_t c7=0;
    DongleLink l7([&](const uint8_t*d,size_t n){ w7.append((const char*)d,n); }, [&](){ return c7; });
    auto drain7=[&](){ for(int i=0;i<64 && !l7.txIdle();i++){ c7+=DongleLink::TX_GAP_MS; l7.poll(); } };
    l7.poll(); drain7();
    CH("detect: DOMO/V3 probed first", w7.find("GET_CDCDEVICE3_VERSION=0; BL=999; APP=201; REV=12201; DT=3; ") != std::string::npos);
    w7.clear();
    c7 += DongleLink::STAGE_TIMEOUT_MS; l7.poll(); drain7();
    CH("detect: after STAGE_TIMEOUT_MS, INDUO II 2.28 is probed next (bare CDCDEVICE header, APP=112, DT=0 = FIRENET status dialect)",
       w7.find("GET_CDCDEVICE_VERSION=0; BL=101; APP=112; REV=13301; DT=0; ") != std::string::npos &&
       w7.find("GET_WIFI_VERSION") == std::string::npos);
    w7.clear();
    c7 += DongleLink::STAGE_TIMEOUT_MS; l7.poll(); drain7();
    CH("detect: after another STAGE_TIMEOUT_MS, INDUO V1 is probed (the combined, hardware-validated frame)",
       w7.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; BL=101; APP=111; REV=360; DT=1; ") != std::string::npos);
    w7.clear();
    c7 += DongleLink::STAGE_TIMEOUT_MS; l7.poll(); drain7();
    CH("detect: cycles back to DOMO/V3 after INDUO V1", w7.find("GET_CDCDEVICE3_VERSION=0; ") != std::string::npos);
  }
  // 6) A 2.28 answering the bare CDCDEVICE probe locks generation=1 (the DOMO protocol, see stageProfile) and
  // is told apart from a DOMO answering the SAME "GET_CDCDEVICE_VERSION_FINISHED" text via which stage was
  // actually pending (model_.version_profile), not the reply text (which is identical for both families).
  {
    std::string w8; uint32_t c8=0;
    DongleLink l8([&](const uint8_t*d,size_t n){ w8.append((const char*)d,n); }, [&](){ return c8; });
    l8.debugSetStage(DongleLink::DETECT_V28);
    l8.poll();
    for (char c : std::string("GET_CDCDEVICE_VERSION_FINISHED")) l8.onByte(c);
    c8 += 60; l8.poll();
    CH("detect: 2.28 ack sets generation=1 (reuses the DOMO protocol)", l8.model().generation == 1 && l8.model().version_ack);
    CH("detect: version_profile records DETECT_V28, not DOMO/V3", l8.model().version_profile == DongleLink::DETECT_V28);
  }
  // 7) A FINISHED reply for a stage we are no longer probing (arrived late, after a retry moved us on) is
  // ignored rather than locking onto a family we did not just ask about.
  {
    std::string w9; uint32_t c9=0;
    DongleLink l9([&](const uint8_t*d,size_t n){ w9.append((const char*)d,n); }, [&](){ return c9; });
    l9.debugSetStage(DongleLink::DETECT_V1);
    l9.poll();
    for (char c : std::string("GET_CDCDEVICE_VERSION_FINISHED")) l9.onByte(c);   // stale V3/2.28-shaped reply
    c9 += 60; l9.poll();
    CH("detect: a CDCDEVICE ack while probing V1 is ignored", !l9.model().version_ack && l9.model().generation == 0);
  }
  // 8) Fast reacquire: after a session reset, re-detection starts at the last stage that worked, not at DOMO/V3.
  {
    std::string w10; uint32_t c10=0;
    DongleLink l10([&](const uint8_t*d,size_t n){ w10.append((const char*)d,n); }, [&](){ return c10; });
    l10.debugSetStage(DongleLink::DETECT_V1);
    l10.poll();
    for (char c : std::string("GET_WIFI_VERSION_FINISHED")) l10.onByte(c);
    c10 += 60; l10.poll();
    CH("fast reacquire: V1 acked", l10.model().version_ack && l10.model().generation == 2);
    w10.clear();
    l10.onByte(0x02); l10.onByte('0'); l10.onByte(0x03);   // session reset
    c10 += 60; l10.poll();
    for(int i=0;i<4 && !l10.txIdle();i++){ c10+=DongleLink::TX_GAP_MS; l10.poll(); }   // drain the queued frame
    CH("fast reacquire: re-probes V1 directly, not DOMO/V3", w10.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ") != std::string::npos);
  }

  // 9) INDUO II 2.28: status is requested right away after ack (unlike DOMO/V3, which defers it), with the
  // 8-digit ID the disassembly confirms it needs (issue #4, darkranger555's SONO: no status request meant
  // GET_SENSORS/GET_REVISION went unanswered forever).
  {
    std::string w11; uint32_t c11=0;
    DongleLink l11([&](const uint8_t*d,size_t n){ w11.append((const char*)d,n); }, [&](){ return c11; });
    l11.setCredentials("MonSSID", "MonPass");
    l11.debugSetStage(DongleLink::DETECT_V28);
    l11.poll();
    for(int i=0;i<8 && !l11.txIdle();i++){ c11+=DongleLink::TX_GAP_MS; l11.poll(); }   // drain the pre-ack probe first
    w11.clear();
    for (char c : std::string("GET_CDCDEVICE_VERSION_FINISHED")) l11.onByte(c);
    c11 += 60; l11.poll();
    CH("2.28: generation=1 (DOMO protocol) after ack", l11.model().generation == 1 && l11.model().version_ack);
    for(int i=0;i<8 && !l11.txIdle();i++){ c11+=DongleLink::TX_GAP_MS; l11.poll(); }
    // Read on a real RIKA SONO 2.28 (2026-09-30): the CDC dialect + hex ssid + protocol=3 (the DOMO wire format)
    // was still never answered even with status sent right away -- so a 2.28 now gets the FIRENET dialect (same
    // as V1) instead, not just an earlier CDC-dialect status push.
    CH("2.28: status uses the FIRENET dialect (same as V1), not DOMO's CDC one", w11.find("GET_FIRENET_STATUS=0;\n") != std::string::npos);
    {
      size_t pf = w11.find("POST_FIRENET_STATUS"), pc = w11.find("POST_CDCDEVICE_STATUS"), st = w11.find("GET_FIRENET_STATUS=0;\n");
      CH("2.28 diagnostic: both bare status requests go out before our status",
         pf != std::string::npos && pc != std::string::npos && st != std::string::npos && pf < st && pc < st);
    }
    CH("2.28: status carries an 8-digit ID, not DOMO/V3's 7-digit default",
       w11.find("\n00000000\n") != std::string::npos && w11.find("\n0000000\n") == std::string::npos);
    CH("2.28: ssid sent in plain text, not hex-encoded like DOMO's", w11.find("\nMonSSID\n") != std::string::npos);
    // Sensor/control tables are DOMO-identical (unshifted), unlike V1's: registering must NOT skip DOMO
    // position 2 (sensors) or 5 (controls, bakeTarget) the way registerV1Sensors/Controls do for a real V1.
    w11.clear();
    l11.pollSensors();
    for(int i=0;i<8 && !l11.txIdle();i++){ c11+=DongleLink::TX_GAP_MS; l11.poll(); }
    CH("2.28: sensor registration keeps DOMO position 2 (bakeTemp), unshifted", w11.find("GET_SENSORS=0; roomTemp=0; flame=0; bakeTemp=0; errMask32=0; ") != std::string::npos);
    w11.clear();
    l11.pollControls();
    for(int i=0;i<8 && !l11.txIdle();i++){ c11+=DongleLink::TX_GAP_MS; l11.poll(); }
    CH("2.28: control registration keeps bakeTarget (DOMO 5), unshifted", w11.find("bakeTarget=0; ") != std::string::npos);
  }
  // 10) DOMO/V3 unchanged: no status pushed right after ack (still deferred to the .ino's own phase logic),
  // and its own 7-digit ID default is untouched.
  {
    std::string w12; uint32_t c12=0;
    DongleLink l12([&](const uint8_t*d,size_t n){ w12.append((const char*)d,n); }, [&](){ return c12; });
    l12.debugSetStage(DongleLink::DETECT_V3);
    l12.poll();
    for(int i=0;i<8 && !l12.txIdle();i++){ c12+=DongleLink::TX_GAP_MS; l12.poll(); }   // drain the pre-ack probe first
    w12.clear();
    for (char c : std::string("GET_CDCDEVICE_VERSION_FINISHED")) l12.onByte(c);
    c12 += 60; l12.poll();
    for(int i=0;i<8 && !l12.txIdle();i++){ c12+=DongleLink::TX_GAP_MS; l12.poll(); }
    CH("DOMO/V3: no status pushed automatically right after ack", w12.empty());
    w12.clear();
    l12.pushStatus();
    for(int i=0;i<4 && !l12.txIdle();i++){ c12+=DongleLink::TX_GAP_MS; l12.poll(); }
    CH("DOMO/V3: pushStatus() still uses its proven 7-digit ID default", w12.find("\n0000000\n") != std::string::npos);
  }

  // 11) Liveness ping: an INDUO-family stove only posts changes once names are registered, so the periodic status
  // cycle also sends a bare POST_FIRENET_STATUS, which the stove answers (keeps the 60s no-RX watchdog fed).
  {
    std::string w13; uint32_t c13=0;
    DongleLink l13([&](const uint8_t*d,size_t n){ w13.append((const char*)d,n); }, [&](){ return c13; });
    l13.setCredentials("MonSSID", "MonPass");
    l13.debugSetStage(DongleLink::DETECT_V1);
    l13.poll();
    for (char c : std::string("GET_WIFI_VERSION_FINISHED")) l13.onByte(c);
    c13 += 60; l13.poll();
    for(int i=0;i<8 && !l13.txIdle();i++){ c13+=DongleLink::TX_GAP_MS; l13.poll(); }
    w13.clear();
    l13.requestStatus();
    for(int i=0;i<8 && !l13.txIdle();i++){ c13+=DongleLink::TX_GAP_MS; l13.poll(); }
    size_t push = w13.find("GET_FIRENET_STATUS=0;\n"), ping = w13.find("POST_FIRENET_STATUS");
    CH("V1 status cycle: pushes GET_FIRENET_STATUS then pings with a bare POST_FIRENET_STATUS",
       push != std::string::npos && ping != std::string::npos && ping > push);
    uint32_t before = l13.model().last_rx_ms;
    c13 += 500;
    std::string reply = "POST_FIRENET_STATUS=0;\n0\n1\n0\n0\n1\n4\n0\n101\n111\n360\n0\n-55\n00000000\n00000000\n1\nMonSSID\nMonPass\n192.168.0.52\nAA:BB\n";
    for (char c : reply) l13.onByte(c);
    c13 += 60; l13.poll();
    CH("V1 ping reply refreshes last_rx and keeps the link acked",
       l13.model().last_rx_ms > before && l13.model().version_ack && l13.model().status.at("ssid") == "MonSSID");

    std::string w14; uint32_t c14=0;
    DongleLink l14([&](const uint8_t*d,size_t n){ w14.append((const char*)d,n); }, [&](){ return c14; });
    l14.debugSetStage(DongleLink::DETECT_V3);
    l14.poll();
    for (char c : std::string("GET_CDCDEVICE_VERSION_FINISHED")) l14.onByte(c);
    c14 += 60; l14.poll();
    for(int i=0;i<8 && !l14.txIdle();i++){ c14+=DongleLink::TX_GAP_MS; l14.poll(); }
    w14.clear();
    l14.requestStatus();
    for(int i=0;i<8 && !l14.txIdle();i++){ c14+=DongleLink::TX_GAP_MS; l14.poll(); }
    CH("DOMO/V3 status cycle unchanged (bare POST_CDCDEVICE_STATUS only)", w14 == "POST_CDCDEVICE_STATUS");
  }

  // Sensor names: the stove copies each registered name into a 32-byte field without a length check, and receives
  // at most 2048 bytes per frame; its POST_SENSORS builder also formats into a ~2 KB stack buffer.
  {
    size_t longest = 0, v1reg = 15, v28reg = 15, v28post = 16;
    for (int p = 0; p < DongleLink::V1_SENSOR_COUNT; p++) { std::string n = sensName(p, 2); longest = std::max(longest, n.size()); v1reg += n.size() + 4; }
    for (int p = 0; p < DongleLink::V28_SENSOR_COUNT; p++) { std::string n = sensName(p); v28reg += n.size() + 4; v28post += n.size() + 1 + 7 + 2; }
    CH("sensor names fit the stove's 32-byte name field", longest <= 31);
    CH("sensor registration frames stay well under the stove's 2048-byte receive limit", v1reg < 1600 && v28reg < 1600);
    CH("a full POST_SENSORS with 7-digit values stays under 2048 bytes", v28post < 2048);
  }

  // INDUO 2.26/2.27: the extended GET_CONTROLS=1 frame is in DOMO order (bakeTarget at 5), which would shift every
  // record from 5 on for a stove whose table has no bakeTarget; only the five-field frame is sent there.
  {
    std::string w; uint32_t c=0;
    DongleLink v1([&](const uint8_t*d,size_t n){ w.append((const char*)d,n); }, [&](){ return c; });
    v1.debugSetStage(DongleLink::DETECT_V1);
    v1.poll();
    for (char ch : std::string("GET_WIFI_VERSION_FINISHED")) v1.onByte(ch);
    c += 60; v1.poll();
    for(int i=0;i<16 && !v1.txIdle();i++){ c+=DongleLink::TX_GAP_MS; v1.poll(); }
    w.clear();
    v1.applyControls({{"onOff",1},{"frostProtectionActive",1}});
    for(int i=0;i<16 && !v1.txIdle();i++){ c+=DongleLink::TX_GAP_MS; v1.poll(); }
    CH("V1 applyControls sends the five-field frame", w.find("GET_CONTROLS=1; revision=") != std::string::npos && w.find("onOff=1; ") != std::string::npos);
    CH("V1 applyControls never sends the DOMO-ordered extended frame", w.find("bakeTarget=") == std::string::npos && w.find("frostProtectionActive=") == std::string::npos);

    std::string wd; uint32_t cd=0;
    DongleLink domo([&](const uint8_t*d,size_t n){ wd.append((const char*)d,n); }, [&](){ return cd; });
    domo.debugSetStage(DongleLink::DETECT_V3);
    domo.poll();
    for (char ch : std::string("GET_CDCDEVICE_VERSION_FINISHED")) domo.onByte(ch);
    cd += 60; domo.poll();
    for(int i=0;i<16 && !domo.txIdle();i++){ cd+=DongleLink::TX_GAP_MS; domo.poll(); }
    wd.clear();
    domo.applyControls({{"onOff",1},{"frostProtectionActive",1}});
    for(int i=0;i<16 && !domo.txIdle();i++){ cd+=DongleLink::TX_GAP_MS; domo.poll(); }
    CH("DOMO/2.29 applyControls still sends the extended frame", wd.find("bakeTarget=") != std::string::npos && wd.find("frostProtectionActive=1; ") != std::string::npos);
    CH("DOMO/2.29 extended frame carries ecoMode as record 6 (official name, was reserved6)", wd.find("bakeTarget=180; ecoMode=0; heatTimeMon1=") != std::string::npos);
    wd.clear();
    domo.applyControls({{"ecoMode",1}});
    for(int i=0;i<16 && !domo.txIdle();i++){ cd+=DongleLink::TX_GAP_MS; domo.poll(); }
    CH("eco mode command is sent in the extended frame", wd.find("ecoMode=1; ") != std::string::npos);
    CH("eco mode kept in the model", domo.model().controls.at("ecoMode") == 1 && domo.model().controls_pos.size() > 6 && domo.model().controls_pos[6] == 1);
  }

  std::cout << ok << " ok, " << ko << " failures\n";
  return ko ? 1 : 0;
}
