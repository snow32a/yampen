#!/bin/sh

set -eu

input="$1"
libdir="$2"
outdir="$3"

mkdir -p "$outdir"

queue="$(mktemp)"
seen="$(mktemp)"

cleanup()
{
	rm -f "$queue" "$seen"
}

trap cleanup EXIT

printf '%s\n' "$input" > "$queue"

while [ -s "$queue" ]; do
	file="$(head -n 1 "$queue")"
	sed -i '1d' "$queue"

	name="$(basename "$file")"

	if grep -Fxq "$name" "$seen"; then
		continue
	fi

	echo "$name" >> "$seen"

	echo "Scanning: $name"

	deps="$(
		objdump -p "$file" 2>/dev/null |
		sed -n \
			-e 's/^[[:space:]]*DLL Name: //p' \
			-e 's/^[[:space:]]*NEEDED[[:space:]]*//p'
	)"

	printf '%s\n' "$deps" |
	while IFS= read -r dep; do
		[ -n "$dep" ] || continue

		case "$dep" in
			kernel32.dll|kernelbase.dll|ntdll.dll|\
			user32.dll|gdi32.dll|advapi32.dll|shell32.dll|\
			ole32.dll|oleaut32.dll|ws2_32.dll|\
			secur32.dll|crypt32.dll|bcrypt.dll)
				continue
				;;
		esac

		src="$libdir/$dep"
		dst="$outdir/$dep"

		if [ -f "$src" ]; then
			if [ ! -f "$dst" ]; then
				echo "  + $dep"
				cp "$src" "$dst"
				printf '%s\n' "$dst" >> "$queue"
			fi
		else
			echo "  ? missing: $dep"
		fi
	done
done