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

    // Poêle INDUO répond POST_FIRENET_STATUS=0;
    std::string st_v1="POST_FIRENET_STATUS=0;\n0\n1\n0\n0\n1\n4\n0\n101\n112\n360\n0\n-55\n0000000\n00000000\n1\nMonSSID\nMonPass\n192.168.1.50\nAA:BB:CC:DD:EE:FF\n-------\n";
    for(char c:st_v1) l5.onByte(c);
    c5+=60; l5.poll();
    CH("V1 status parsed", l5.model().status.at("ssid") == "MonSSID" && l5.model().status.at("symbol") == "4");

    // 2) Télémétrie positionnelle V1 : GET_SENSORS=1; -> POST_SENSORS=0; =val0; =val1; ...
    w5.clear();
    l5.pollSensors(); drain5();
    CH("V1 pollSensors sends GET_SENSORS=1;", w5.find("GET_SENSORS=1; ") != std::string::npos);
    CH("V1 does NOT send sentinels", w5.find("s00=0") == std::string::npos);

    // Réponse poêle INDUO V1 (PRIO 1) : 13 valeurs positionnelles
    std::string sens_v1 = "POST_SENSORS=0; =215; =450; =0; =0; =650; =0; =75; =1450; =50; =3600; =1200; =4200; =1; ";
    for(char c:sens_v1) l5.onByte(c);
    c5+=60; l5.poll();
    CH("V1 sensors_pos size 13", l5.model().sensors_pos.size() == 13);
    CH("V1 roomTemp mapped", l5.model().sensors.at("roomTemp") == 215);
    CH("V1 flame mapped", l5.model().sensors.at("flame") == 450);
    CH("V1 augerSet mapped", l5.model().sensors.at("augerSet") == 75);
    CH("V1 idFanMeas mapped", l5.model().sensors.at("idFanMeas") == 1450);
    CH("V1 pelletHours mapped", l5.model().sensors.at("pelletHours") == 3600);
    CH("V1 pelletsTotal mapped", l5.model().sensors.at("pelletsTotal") == 4200);

    // 3) Réinitialisation par STX '0' ETX (\x02 0 \x03)
    w5.clear();
    l5.onByte(0x02); l5.onByte('0'); l5.onByte(0x03);
    c5+=60; l5.poll();
    CH("V1 STX 0 ETX clears version_ack immediately", !l5.model().version_ack);
    drain5();
    CH("V1 handshake re-armed after session reset", w5.find("GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ") != std::string::npos);
  }

  std::cout << ok << " ok, " << ko << " failures\n";
  return ko ? 1 : 0;
}
