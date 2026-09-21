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
  link.poll(); drain();              // empile + émet la version
  CH("version émise", wire.find("GET_CDCDEVICE3_VERSION=0; BL=112; APP=201; REV=12201; DT=3;")!=std::string::npos);
  // le poêle répond FINISHED
  std::string fin="GET_CDCDEVICE_VERSION_FINISHED";
  for(char c:fin) link.onByte(c);
  clk+=60; link.poll();              // silence écoulé (>SILENCE_MS après le dernier octet)
  CH("version acquittée", link.model().version_ack && link.model().generation==1);
  // le poêle pousse un POST_CDCDEVICE_STATUS avec SSID hexa
  wire.clear();
  std::string st="POST_CDCDEVICE_STATUS=0;\n0\n1\n0\n0\n5\n0\n0\n112\n201\n12201\n0\n-52\n17800020\nfHTeLam2\n3\n4D6F6E53534944\nsecret\n192.168.1.5\nAA:BB\n1\n";
  for(char c:st) link.onByte(c);
  clk+=60; link.poll();              // silence écoulé
  CH("ssid décodé", link.model().status.at("ssid")=="MonSSID");
  CH("app_version lu", link.model().status.at("app_version")=="201");
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
  }
  std::cout << ok << " ok, " << ko << " failures\n";
  return ko ? 1 : 0;
}
