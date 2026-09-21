#!/usr/bin/env python3
"""
test_stove_sim.py — Simulateur matériel de poêle RIKA (INDUO V1 & DOMO V3)
Couplé avec /tmp/host_bridge (firmware C++ réel DongleLink) pour valider
l'intégralité du cycle de communication et de la machine à états.
"""

import subprocess
import time
import sys

def to_hex(s: str or bytes) -> str:
    if isinstance(s, str):
        s = s.encode('latin1')
    return s.hex()

def from_hex(h: str) -> str:
    return bytes.fromhex(h).decode('latin1', errors='replace')

class HostBridgeDriver:
    def __init__(self, bin_path="/tmp/host_bridge"):
        self.proc = subprocess.Popen(
            [bin_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

    def send_cmd(self, cmd: str) -> list:
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        out_tx = []
        state = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                break
            line = line.strip()
            if line.startswith("TX "):
                raw_hex = line[3:].strip()
                out_tx.append(from_hex(raw_hex))
            elif line.startswith("STATE "):
                state = line
            elif line == "OK":
                break
            elif line.startswith("ERR"):
                print(f"Bridge error: {line}")
        return out_tx, state

    def rx(self, s: str or bytes):
        return self.send_cmd(f"RX {to_hex(s)}")

    def tick(self, ms: int):
        return self.send_cmd(f"TICK {ms}")

    def get_state(self):
        _, st = self.send_cmd("STATE")
        return st

    def close(self):
        self.proc.terminate()
        self.proc.wait()


class InduoV1StoveSimulator:
    """
    Simule la machine à états et la pile CDC d'une carte mère INDUO V2.27
    (firmware RIKA_a_001_227_Application_INDUO_V2.27.445.01.bin).
    """
    def __init__(self):
        self.session_linked = False  # *0x1ac8
        self.watchdog_ticks = 100    # *0x1ac4 (~12s)
        self.state_get = 0           # *0x57e5 (bloque GET_* si 0)
        self.state_post = 0          # *0x57e2 (bloque POST_* si 0)
        self.state_transfer = 0      # *0x57e6 (bloque TRANSFER si 0)

        # Registres capteurs poêle
        self.room_temp = 215         # 21.5°C (scale 10)
        self.flame_temp = 480        # 480°C
        self.error_code = 0
        self.warning_code = 0
        self.service_countdown = 680 # kg
        self.discharge_rpm = 0
        self.auger_set = 80          # RPM
        self.id_fan_rpm = 1520       # RPM
        self.air_flaps = 60
        self.pellet_hours = 3850     # min
        self.log_hours = 1240        # min
        self.pellets_total = 4520    # kg
        self.stove_on = 1

        # Contrôles
        self.ctrl_on = 1
        self.ctrl_mode = 2
        self.ctrl_stage = 75
        self.ctrl_room = 210

    def process_dongle_tx(self, frame: str) -> list:
        """
        Traite une trame émise par le dongle et renvoie les octets ou trames générées par le poêle.
        """
        replies = []

        # 1. Version Handshake
        if "GET_WIFI_VERSION=0;" in frame:
            # Vérifications de conformité matérielle stricte INDUO V1 (VA 0x800375a0):
            if "GET_CDCDEVICE" in frame:
                # REJET : nom erroné
                return []
            if "DT=" in frame:
                # REJET : DT non supporté en V1
                return []
            if "BL=101;" in frame and "APP=112;" in frame:
                # Version validée par la carte mère
                self.session_linked = True
                self.watchdog_ticks = 100
                replies.append("GET_WIFI_VERSION_FINISHED\r\n")
                return replies

        # 2. Status Handshake (Déverrouillage obligatoire de *0x57e5, *0x57e2, *0x57e6)
        if "GET_FIRENET_STATUS=0;\n" in frame:
            lines = frame.split("\n")
            if len(lines) >= 20:  # En-tête + 19 paramètres
                self.state_get = 1
                self.state_post = 1
                self.state_transfer = 1
                self.watchdog_ticks = 100
                # Réponse du poêle
                st_reply = (
                    "POST_FIRENET_STATUS=0;\n"
                    "0\n1\n0\n0\n1\n4\n0\n101\n112\n360\n0\n-55\n"
                    "0000000\n00000000\n1\nMonSSID\nMonPass\n192.168.1.50\nAA:BB:CC:DD:EE:FF\n"
                    "-------\n"
                )
                replies.append(st_reply)
                return replies

        # SI LE STATUT N'A PAS ÉTÉ VALIDÉ, LA CARTE MÈRE JETTE TOUT SILENCIEUSEMENT !
        if self.state_get == 0:
            return []

        # 3. Requête capteurs PRIO 1 (GET_SENSORS=1;)
        if "GET_SENSORS=1;" in frame:
            self.watchdog_ticks = 100
            # Réponse positionnelle sans clés
            sens_frame = (
                f"POST_SENSORS=0; ={self.room_temp}; ={self.flame_temp}; ={self.error_code}; "
                f"={self.warning_code}; ={self.service_countdown}; ={self.discharge_rpm}; "
                f"={self.auger_set}; ={self.id_fan_rpm}; ={self.air_flaps}; ={self.pellet_hours}; "
                f"={self.log_hours}; ={self.pellets_total}; ={self.stove_on}; "
            )
            replies.append(sens_frame)
            return replies

        # 4. Requête lecture contrôles (GET_CONTROLS=0;)
        if "GET_CONTROLS=0;" in frame:
            self.watchdog_ticks = 100
            ctrl_frame = (
                f"POST_CONTROLS=0; onOff={self.ctrl_on}; mode={self.ctrl_mode}; "
                f"targetStage={self.ctrl_stage}; roomTarget={self.ctrl_room}; "
            )
            replies.append(ctrl_frame)
            return replies

        # 5. Modification de consigne (GET_CONTROLS=1; ...)
        if "GET_CONTROLS=1;" in frame:
            self.watchdog_ticks = 100
            # Parse les consignes reçues
            for part in frame.split(";"):
                part = part.strip()
                if part.startswith("onOff="):
                    self.ctrl_on = int(part.split("=")[1])
                elif part.startswith("mode="):
                    self.ctrl_mode = int(part.split("=")[1])
                elif part.startswith("targetStage="):
                    self.ctrl_stage = int(part.split("=")[1])
                elif part.startswith("roomTarget="):
                    self.ctrl_room = int(part.split("=")[1])
            ctrl_frame = (
                f"POST_CONTROLS=0; onOff={self.ctrl_on}; mode={self.ctrl_mode}; "
                f"targetStage={self.ctrl_stage}; roomTarget={self.ctrl_room}; "
            )
            replies.append(ctrl_frame)
            return replies

        return replies


def run_induo_simulation_tests():
    print("=== Démarrage des Tests de Simulation Poêle INDUO V1 ===")
    bridge = HostBridgeDriver("/tmp/host_bridge")
    stove = InduoV1StoveSimulator()

    passed = 0
    failed = 0

    def assert_test(desc: str, condition: bool):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  [OK] {desc}")
        else:
            failed += 1
            print(f"  [FAIL] {desc}")

    # Etape 1 : Le poêle envoie la sonde de boot \x16 3
    print("\n--- 1. Émission de la sonde de boot (SYN '3') ---")
    tx_list, _ = bridge.rx("\x163")
    # Le dongle doit répondre avec GET_WIFI_VERSION=0; BL=101; APP=112; REV=360;
    tx_all, _ = bridge.tick(100)
    tx_list += tx_all

    version_frame = next((t for t in tx_list if "GET_WIFI_VERSION=0;" in t), None)
    assert_test("Le dongle répond à la sonde avec GET_WIFI_VERSION=0;", version_frame is not None)
    assert_test("La trame n'a PAS de DT=1", "DT=" not in (version_frame or ""))
    assert_test("La trame n'a PAS de CDCDEVICE", "CDCDEVICE" not in (version_frame or ""))

    # Etape 2 : Le poêle valide la version et répond FINISHED
    print("\n--- 2. Validation de la version par le poêle ---")
    stove_replies = stove.process_dongle_tx(version_frame or "")
    assert_test("Le poêle accepte la version V1", len(stove_replies) == 1 and "FINISHED" in stove_replies[0])

    # Le dongle reçoit GET_WIFI_VERSION_FINISHED
    tx_after_ack, _ = bridge.rx(stove_replies[0])
    # Attente du silence et dispatch
    tx_after_ack += bridge.tick(60)[0]

    # Etape 3 : Le dongle doit IMMEDIATEMENT envoyer GET_FIRENET_STATUS=0; pour déverrouiller la SRAM
    print("\n--- 3. Déverrouillage de la machine à états SRAM (Status Handshake) ---")
    # Draine la file TX
    for _ in range(10):
        tx_drain, _ = bridge.tick(600)
        tx_after_ack += tx_drain

    status_push = next((t for t in tx_after_ack if "GET_FIRENET_STATUS=0;\n" in t), None)
    assert_test("Le dongle a envoyé GET_FIRENET_STATUS=0; immédiatement", status_push is not None)

    # Le poêle reçoit le statut, déverrouille sa SRAM et répond POST_FIRENET_STATUS
    stove_replies2 = stove.process_dongle_tx(status_push or "")
    assert_test("La carte mère du poêle déverrouille sa SRAM (*0x57e5=1)", stove.state_get == 1)
    assert_test("Le poêle émet POST_FIRENET_STATUS=0;", len(stove_replies2) == 1 and "POST_FIRENET_STATUS" in stove_replies2[0])

    bridge.rx(stove_replies2[0])
    bridge.tick(60)

    # Etape 4 : Interrogation des capteurs PRIO 1 (POLLSENS)
    print("\n--- 4. Télémétrie Capteurs PRIO 1 (Positionnel) ---")
    tx_poll, _ = bridge.send_cmd("POLLSENS")
    for _ in range(5):
        tx_poll += bridge.tick(600)[0]

    sens_req = next((t for t in tx_poll if "GET_SENSORS=1;" in t), None)
    assert_test("Le dongle interroge les capteurs avec GET_SENSORS=1; (sans sentinelles)", sens_req is not None)

    stove_sens_replies = stove.process_dongle_tx(sens_req or "")
    assert_test("Le poêle répond avec les valeurs positionnelles", len(stove_sens_replies) == 1 and "=215;" in stove_sens_replies[0])

    # Le dongle ingère la télémétrie positionnelle
    bridge.rx(stove_sens_replies[0])
    bridge.tick(60)

    # Vérification du modèle de données interne du firmware
    state_str = bridge.get_state()
    assert_test("roomTemp extrait à 21.5°C (215)", "roomTemp=215" in state_str)
    assert_test("sensors_pos correctement alimenté (13 positions)", "spn=13" in state_str)
    assert_test("pelletHours (pos 9) extrait", "sp9=3850" in state_str)
    assert_test("pelletsTotal (pos 11) extrait", "sp11=4520" in state_str)

    # Etape 5 : Réinitialisation de session poêle (STX '0' ETX)
    print("\n--- 5. Simulation Watchdog / Déconnexion du poêle (STX '0' ETX) ---")
    # Le poêle envoie \x02 0 \x03
    tx_reset, _ = bridge.rx("\x020\x03")
    tx_reset += bridge.tick(60)[0]

    state_after_reset = bridge.get_state()
    assert_test("Le dongle a invalidé son ack (ack=0) sur STX 0 ETX", "ack=0" in state_after_reset)

    # Draine la retransmission de version
    for _ in range(5):
        tx_reset += bridge.tick(600)[0]

    rearm_version = next((t for t in tx_reset if "GET_WIFI_VERSION=0;" in t), None)
    assert_test("Le dongle a réémis la trame de version pour réarmer la liaison", rearm_version is not None)

    bridge.close()
    print(f"\nRésultats simulateur INDUO V1 : {passed} succès, {failed} échecs.")
    return failed == 0


class DomoV3StoveSimulator:
    """
    Simule la machine à états et le protocole V3 (DOMO V2.29+).
    """
    def __init__(self):
        self.session_linked = False

    def process_dongle_tx(self, frame: str) -> list:
        replies = []
        if "GET_CDCDEVICE3_VERSION=0;" in frame and "DT=3;" in frame:
            self.session_linked = True
            replies.append("GET_CDCDEVICE_VERSION_FINISHED\r\n")
            return replies

        if "GET_CDCDEVICE_STATUS=0;\n" in frame:
            replies.append(
                "POST_CDCDEVICE_STATUS=0;\n"
                "0\n1\n0\n0\n1\n4\n0\n999\n201\n12201\n0\n-62\n"
                "1234567\n87654321\n3\n4D6F6E53534944\nsecret\n192.168.1.99\n11:22:33:44:55:66\n"
                "-------\n"
            )
            return replies

        if "GET_SENSORS=0;" in frame:
            # Réponse DOMO : 53 capteurs fragmentés en continuations (exact §8.5 / link_test.cpp)
            return [
                "POST_SENSORS=0; =213; =47; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =0; =1; =0; ",
                "=0; =1; =1; =1; =1; =1; =1; =29; =70; =70; =70; =1; =3; =0; =0; =1; =13; =3; =229; =0; =0; =160; ",
                "=150; =112; =58512; =53404; =12201; =4354; =0; =7064; =700; =0; =0; "
            ]

        return replies


def run_domo_v3_simulation_tests():
    print("\n=== Démarrage des Tests de Simulation Poêle DOMO V3 ===")
    bridge = HostBridgeDriver("/tmp/host_bridge")
    stove = DomoV3StoveSimulator()

    passed = 0
    failed = 0

    def assert_test(desc: str, condition: bool):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  [OK] {desc}")
        else:
            failed += 1
            print(f"  [FAIL] {desc}")

    # Etape 1 : Le poêle DOMO ignore la version V1 et attend la version V3
    print("\n--- 1. Négociation automatique et repli vers V3 ---")
    bridge.rx("3")
    bridge.tick(100)

    # Force le cycle de retransmission jusqu'au basculement vers V3 (PROFILE_SWITCH_AFTER = 3)
    tx_frames = []
    for _ in range(4):
        bridge.tick(DongleLink_VERSION_RETRY := 1000)
        for _ in range(3):
            tx_frames += bridge.tick(600)[0]

    v3_frame = next((t for t in tx_frames if "GET_CDCDEVICE3_VERSION=0;" in t), None)
    assert_test("Le dongle a basculé automatiquement vers le profil V3", v3_frame is not None)
    assert_test("Le profil V3 inclut DT=3;", "DT=3;" in (v3_frame or ""))

    # Etape 2 : Le poêle DOMO accepte la version V3
    print("\n--- 2. Validation de la version V3 par le poêle ---")
    stove_replies = stove.process_dongle_tx(v3_frame or "")
    assert_test("Le poêle DOMO répond GET_CDCDEVICE_VERSION_FINISHED", len(stove_replies) == 1 and "CDCDEVICE" in stove_replies[0])

    bridge.rx(stove_replies[0])
    bridge.tick(60)

    state_str = bridge.get_state()
    assert_test("Generation 1 (CDCDEVICE/V3) verrouillée", "gen=1" in state_str and "ack=1" in state_str)

    # Etape 3 : Télémétrie capteurs DOMO V3
    print("\n--- 3. Télémétrie Capteurs V3 (53 slots avec sentinelles) ---")
    tx_sens, _ = bridge.send_cmd("POLLSENS")
    for _ in range(5):
        tx_sens += bridge.tick(600)[0]

    v3_sens_req = next((t for t in tx_sens if "GET_SENSORS=0;" in t), None)
    assert_test("Le dongle V3 émet GET_SENSORS=0; avec liste de sentinelles", v3_sens_req is not None)

    stove_sens_replies = stove.process_dongle_tx(v3_sens_req or "")
    for chunk in stove_sens_replies:
        bridge.rx(chunk)
        bridge.tick(60)

    state_v3 = bridge.get_state()
    assert_test("roomTemp V3 extrait à 21.3°C (213)", "roomTemp=213" in state_v3)
    assert_test("DOMO V3 53 capteurs positionnels ingérés", "spn=53" in state_v3)
    assert_test("pelletHours (pos 47) extrait à 4354", "sp47=4354" in state_v3)
    assert_test("pelletsTotal (pos 49) extrait à 7064", "sp49=7064" in state_v3)

    bridge.close()
    print(f"\nRésultats simulateur DOMO V3 : {passed} succès, {failed} échecs.")
    return failed == 0


if __name__ == "__main__":
    ok_v1 = run_induo_simulation_tests()
    ok_v3 = run_domo_v3_simulation_tests()
    if ok_v1 and ok_v3:
        print("\n=======================================================")
        print("TOUTES LES VALIDATIONS DE SIMULATION ONT RÉUSSI (100%)")
        print("  - RIKA INDUO V1 (V2.26 / V2.27) : OK")
        print("  - RIKA DOMO V3 (V2.29+)         : OK")
        print("=======================================================")
        sys.exit(0)
    else:
        sys.exit(1)
