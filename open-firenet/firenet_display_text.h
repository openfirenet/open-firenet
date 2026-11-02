// The words of the small screen, in the five languages of the page. Texts are UTF-8; the fonts hold the accents these
// languages use (see tools/gen_display_assets.py). Index = DisplayConfig::language: 0 (not chosen yet) shows English.
#pragma once
#include <cstring>

namespace firenet {
namespace screentext {

enum Word { W_STOVE, W_ROOM, W_FLAME, W_COMFORT, W_POWER, W_ADDRESS, W_SERVICE_IN, W_RUNNING, W_WIFI, W_MQTT, W_UPDATE, W_FIRMWARE,
            W_ONLINE, W_OFFLINE, W_STOVE_ERROR, W_ALERT, W_HOPPER_OPEN, W_NO_STOVE, W_NO_WIFI, W_MANUAL, W_AUTO, W_CODE, W_RUNNING_LOGS, W_PELLETS_EMPTY, W_COUNT };

// The states, by the name the bridge gives them in its API ("stove.state").
static const char* const STATE_KEYS[] = {"off", "standby", "ignition", "flame_start", "heating", "cleaning", "burn_off", "splitlog",
                                         "splitlog_check", "splitlog_refuel", "splitlog_no_refuel", "unknown"};
static constexpr int STATE_COUNT = sizeof(STATE_KEYS) / sizeof(STATE_KEYS[0]);

// Rows: en, fr, it, de, es.
static const char* const WORDS[5][W_COUNT] = {
  {"STOVE", "ROOM", "FLAME", "COMFORT", "POWER", "ADDRESS", "SERVICE IN", "RUNNING", "WI-FI", "MQTT", "UPDATE", "FIRMWARE",
   "Online", "Offline", "STOVE ERROR", "ALERT", "Hopper open", "No stove", "No Wi-Fi", "MANUAL", "AUTO", "Code", "LOG HOURS", "Pellet tank empty"},
  {"POÊLE", "PIÈCE", "FLAMME", "CONFORT", "PUISSANCE", "ADRESSE", "ENTRETIEN", "HEURES", "WI-FI", "MQTT", "MISE À JOUR", "FIRMWARE",
   "En ligne", "Hors ligne", "ERREUR", "ALERTE", "Trémie ouverte", "Pas de poêle", "Pas de Wi-Fi", "MANUEL", "AUTO", "Code", "HEURES BOIS", "Réservoir vide"},
  {"STUFA", "STANZA", "FIAMMA", "COMFORT", "POTENZA", "INDIRIZZO", "MANUTENZ.", "ORE", "WI-FI", "MQTT", "AGGIORNA", "FIRMWARE",
   "Online", "Offline", "ERRORE", "ALLARME", "Tramoggia aperta", "Nessuna stufa", "Niente Wi-Fi", "MANUALE", "AUTO", "Codice", "ORE LEGNA", "Serbatoio vuoto"},
  {"OFEN", "RAUM", "FLAMME", "KOMFORT", "LEISTUNG", "ADRESSE", "WARTUNG IN", "BETRIEB", "WLAN", "MQTT", "UPDATE", "FIRMWARE",
   "Online", "Offline", "OFENFEHLER", "ALARM", "Behälter offen", "Kein Ofen", "Kein WLAN", "MANUELL", "AUTO", "Code", "HOLZ-STD.", "Pelletbehälter leer"},
  {"ESTUFA", "SALA", "LLAMA", "CONFORT", "POTENCIA", "DIRECCIÓN", "MANTENIM.", "HORAS", "WI-FI", "MQTT", "ACTUALIZAR", "FIRMWARE",
   "Conectado", "Sin conexión", "ERROR ESTUFA", "ALERTA", "Tolva abierta", "Sin estufa", "Sin Wi-Fi", "MANUAL", "AUTO", "Código", "HORAS LEÑA", "Depósito vacío"},
};

static const char* const STATES[5][STATE_COUNT] = {
  {"Off", "Standby", "Ignition", "Flame Start", "Heating", "Grate Cleaning", "Burn Off", "Split Log", "Split Log Check", "Put On Split Logs", "Do Not Put Logs", "Unknown"},
  {"Arrêt", "Veille", "Allumage", "Démarrage flamme", "Chauffe", "Nettoyage grille", "Extinction", "Mode bûches", "Contrôle bûches", "Recharger en bûches", "Ne pas recharger", "Inconnu"},
  {"Spento", "Standby", "Accensione", "Avvio fiamma", "Riscaldamento", "Pulizia griglia", "Spegnimento", "Legna", "Controllo legna", "Aggiungere legna", "NON aggiungere legna", "Sconosciuto"},
  {"Aus", "Bereitschaft", "Zündung", "Flammenstart", "Heizen", "Rostreinigung", "Abbrand", "Scheitholz", "Scheitholz-Check", "Bitte Nachlegen", "NICHT Nachlegen", "Unbekannt"},
  {"Apagada", "En espera", "Encendido", "Inicio de llama", "Calentando", "Limpieza de parrilla", "Apagado de llama", "Leña", "Control de leña", "Añadir leña", "NO añadir leña", "Desconocido"},
};

// language: 0 not chosen (English), 1 en, 2 fr, 3 it, 4 de, 5 es.
inline int row(int language) { return language >= 2 && language <= 5 ? language - 1 : 0; }
inline const char* word(int language, Word w) { return WORDS[row(language)][w]; }
// The state's text in the language, or `fallback` when the key is unknown (a state a later firmware adds).
inline const char* stateName(int language, const char* key, const char* fallback) {
  for (int i = 0; i < STATE_COUNT; i++) if (strcmp(key, STATE_KEYS[i]) == 0) return STATES[row(language)][i];
  return fallback;
}

}  // namespace screentext
}  // namespace firenet
