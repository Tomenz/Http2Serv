# Datei: generate_bytes.py

# Ziel: Eine Datei mit 123456 Bytes erzeugen
# Inhalt: fortlaufende Zeichen von chr(32) bis chr(98), wiederholt

def main():
    total_bytes = 123456
    with open("output.bin", "wb") as f:
        for i in range(total_bytes):
            f.write(bytes([i % 256]))  # schreibt Bytewerte 0-255 im Zyklus

    start = 32     # Zeichen ' '
    end = 127      # Zeichen '~'

    # Erzeuge Byte-Sequenz von 32 bis 127
    sequence = bytes(range(start, end + 1))

    # Wiederhole sie, bis 123456 Bytes erreicht sind
    data = (sequence * (total_bytes // len(sequence) + 1))[:total_bytes]

    # In Datei schreiben
    with open("output.txt", "wb") as f:
        f.write(data)

    print(f"Datei 'output.txt' wurde erstellt ({len(data)} Bytes).")

if __name__ == "__main__":
    main()
