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
  // négociation
  link.poll(); drain();              // queue + emit the version (V1 profile by default)
  CH("V1 version emitted", wire.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ")!=std::string::npos);
  // le poêle répond FINISHED
  std::string fin="GET_CDCDEVICE_VERSION_FINISHED";
  for(char c:fin) link.onByte(c);
  clk+=60; link.poll();              // silence écoulé (>SILENCE_MS après le dernier octet)
  CH("version acquittée", link.model().version_ack && link.model().generation==1);
  CH("V1 profile locked after ACK", link.model().version_profile==1);
  // stove emits POST_CDCDEVICE_STATUS with plain text SSID (DT=1, 19 fields)
  wire.clear();
  std::string st="POST_CDCDEVICE_STATUS=0;\n0\n1\n0\n0\n5\n0\n0\n101\n112\n360\n0\n-52\n17800020\nfHTeLam2\n3\nMonSSID\nsecret\n192.168.1.5\nAA:BB\n";
  for(char c:st) link.onByte(c);
  clk+=60; link.poll();              // silence elapsed
  CH("plain ssid parsed", link.model().status.at("ssid")=="MonSSID");
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
    c4+=DongleLink::TX_GAP_MS; l4.poll();
    CH("handshake re-sent after recovery", w4.find("GET_WIFI_VERSION")!=std::string::npos || w4.find("GET_CDCDEVICE")!=std::string::npos);
  }

  // --- Tests spécifiques Firenet V1 (INDUO V2.26 / V2.27) ---
  // 1) Handshake V1 : envoi GET_WIFI_VERSION -> réception GET_WIFI_VERSION_FINISHED -> push immédiat de GET_FIRENET_STATUS
  {
    std::string w5; uint32_t c5=0;
    DongleLink l5([&](const uint8_t*d,size_t n){ w5.append((const char*)d,n); },
                  [&](){ return c5; });
    l5.setCredentials("MonSSID", "MonPass", "192.168.1.50", "AA:BB:CC:DD:EE:FF");
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
         w5.find("GET_SENSORS=0; roomTemp=0; flame=0; errMask32=0; errSub=0; stateMask=0; s06=0; augerSet=0; ") != std::string::npos);
      CH("V1 registration has V1 position 30 = mainState (DOMO 31) and 45 = appRevision (DOMO 46)",
         w5.find("stageCur=0; mainState=0; subState=0; rssi=0; ") != std::string::npos && w5.find("appRevision=0; pelletHours=0; ") != std::string::npos);
      CH("V1 registration contains onOffCycles then the unlabelled DOMO 55.. as sNN", w5.find("ignitionCount=0; onOffCycles=0; s55=0; s56=0; ") != std::string::npos);
      {
        size_t end = w5.find("s86=0; s87=0; ");
        CH("V1 registration ends with DOMO 87 (V1 position 86, the last record the stove fills)",
           end != std::string::npos && end + 14 <= w5.size() && w5.compare(end + 14, 13, "GET_REVISION=") == 0);
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
    l6.onDebug([&](const char* dir, const std::string&){ if (std::string(dir)=="syn") syn_lines++; });
    l6.onByte(0x16); l6.poll();
    CH("SYN: first 0x16 answered immediately", w6.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ") != std::string::npos);
    for (int i=0;i<250;i++) { c6+=8; l6.onByte(0x16); l6.poll(); }   // ~2 s de 0x16 toutes les 8 ms
    size_t frames=0; for (size_t p=0; (p=w6.find("GET_WIFI_VERSION", p)) != std::string::npos; p++) frames++;
    CH("SYN: reply rate capped (<= 1 per TX_GAP_MS)", frames <= 2000/DongleLink::TX_GAP_MS + 3);
    CH("SYN: every 0x16 counted", l6.synCount() == 251);
    CH("SYN: summarised, not logged per byte", syn_lines >= 1 && syn_lines <= 4);
  }

  std::cout << ok << " ok, " << ko << " failures\n";
  return ko ? 1 : 0;
}
