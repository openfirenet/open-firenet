// Parité g++ du cœur protocole Open-Firenet.
// Compile : g++ -std=c++17 firenet_protocol_test.cpp -o /tmp/ft && /tmp/ft
#include "firenet_protocol.h"
#include <cassert>
#include <iostream>
using namespace firenet;
static int ok=0, ko=0;
#define CHECK(sec,name,cond) do{ if(cond) ok++; else {ko++; \
  std::cout<<"ECHEC ["<<sec<<"] "<<name<<"\n";} }while(0)

int main(){
  // §4.1 filtre
  for(uint8_t b : {0x16,0x00,0x01,0x7F,0x80,0xFF}) CHECK("4.1","rejet",!byteAccepted(b));
  for(uint8_t b : {0x02,0x03,0x06,0x09,0x0A,0x0D,0x15,0x20,0x33,0x7E}) CHECK("4.1","accept",byteAccepted(b));

  // §5.3 codec hexa
  CHECK("5.3","maj", hexEncode(std::string(1,(char)0xAB))=="AB");
  CHECK("5.3","aller-retour", hexDecode(hexEncode("MonSSID"))=="MonSSID");
  CHECK("5.3","minuscules", hexDecode("6d6f6e")=="mon");
  CHECK("5.3","borne 31 octets", hexDecode(std::string(80,'4')).size()==31);
  CHECK("5.3","MonSSID connu", hexEncode("MonSSID")=="4D6F6E53534944");

  // §7.2 tokeniseur : jetons vides conservés
  auto v = parseStatusFrame("X=0;\na\n\nb\n");
  CHECK("7.2","3 jetons dont un vide", v.size()==3 && v[0]=="a" && v[1]=="" && v[2]=="b");

  // trame status complète 23 champs
  std::string f = "GET_CDCDEVICE_STATUS=0;\n2\n1\n0\n0\n1\n0\n0\n112\n201\n12201\n0\n-52\n"
                  "17800020\nfHTeLam2\n0\n4D6F6E53534944\nMonMotDePasse\n192.168.1.42\n"
                  "AA:BB:CC:DD:EE:FF\n1\n0\n0\n0\n";
  auto w = parseStatusFrame(f);
  CHECK("7.2","23 champs", w.size()==23);
  CHECK("7.2","ssid hexa", w.size()>15 && w[15]=="4D6F6E53534944");
  CHECK("5.3","ssid décodé", w.size()>15 && hexDecode(w[15])=="MonSSID");

  // trame avec champs chaîne vides (id/token/ip/mac vides)
  std::string e = "POST_CDCDEVICE_STATUS=0;\n0\n1\n0\n0\n0\n0\n0\n112\n201\n12201\n0\n0\n\n\n0\nAB\nCD\n\n\n1\n";
  auto x = parseStatusFrame(e);
  CHECK("7.2","champs vides -> pas de décalage", x.size()>=17 && x[12]=="" && x[13]=="" && x[15]=="AB");

  // §12 constantes de version
  CHECK("12","APP=201", APP_VERSION==201);
  CHECK("12","DT=3", DT==3);
  CHECK("5","23 champs déclarés", NUM_FIELDS==23);
  CHECK("13","room target ×10", CTRL_ROOM_TARGET_SCALE==10);

  // libellés positionnels (§13/§14)
  CHECK("13","ctrl 1 = onOff", ctrlName(1)=="onOff");
  CHECK("13","ctrl 3 = targetStage", ctrlName(3)=="targetStage");
  CHECK("13","ctrl 23 = convectionFan1Active", ctrlName(23)=="convectionFan1Active");
  CHECK("13","ctrl 24 = convectionFan1Level", ctrlName(24)=="convectionFan1Level");
  CHECK("13","ctrl 25 = convectionFan1Area", ctrlName(25)=="convectionFan1Area");
  CHECK("13","ctrl 26 = convectionFan2Active", ctrlName(26)=="convectionFan2Active");
  CHECK("13","ctrl 27 = convectionFan2Level", ctrlName(27)=="convectionFan2Level");
  CHECK("13","ctrl 28 = convectionFan2Area", ctrlName(28)=="convectionFan2Area");
  CHECK("14","sens 0 = roomTemp", sensName(0)=="roomTemp");
  CHECK("14","sens 31 = mainState", sensName(31)=="mainState");
  CHECK("14","sens 33 = rssi", sensName(33)=="rssi");
  CHECK("13","control non identifié -> cNN", ctrlName(35)=="c35");

  // §5.4 log password redaction
  std::string sanF = sanitizeForLog(f);
  CHECK("5.4","wpa2 redacted", sanF.find("MonMotDePasse") == std::string::npos && sanF.find("********") != std::string::npos);
  auto sanFields = parseStatusFrame(sanF);
  CHECK("5.4","all other fields intact", sanFields.size()==23 && sanFields[16]=="********" && sanFields[15]=="4D6F6E53534944" && sanFields[0]=="2" && sanFields[7]=="112");
  std::string noStatus = "POST_CONTROLS=1; onOff=1; roomTarget=200; MonMotDePasse=0; ";
  CHECK("5.4","non-status frame untouched", sanitizeForLog(noStatus) == noStatus);
  std::string emptyPass = "GET_CDCDEVICE_STATUS=0;\n2\n1\n0\n0\n1\n0\n0\n112\n201\n12201\n0\n-52\n17800020\nfHTeLam2\n0\n4D6F6E53534944\n\n192.168.1.42\nAA:BB:CC:DD:EE:FF\n1\n0\n0\n0\n";
  CHECK("5.4","empty password stays empty", sanitizeForLog(emptyPass) == emptyPass);

  std::cout << ok << " checks passed, " << ko << " failures\n";
  return ko?1:0;
}
