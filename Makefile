# Makefile — ABDSharedCode
#
# Targets de conveniencia para desarrollo local. La verificacion de estilo
# esta centralizada en `.clang-format`, el pre-commit hook (`.githooks/`) y
# el CI (`.github/workflows/shared-code-ci.yml`). Este Makefile envuelve
# clang-format para uso interactivo fuera del flujo de comentarios.
#
# Requiere: clang-format 18+ en el PATH. Si no lo encuentra, los targets
# fallan con un mensaje que indica como instalarlo.

# ── clang-format detection (≥ 18) ─────────────────────────────────────────────

REQUIRED_MAJOR := 18
# Los binarios versionados tienen prioridad (clang-format-18), luego el
# nombre generico. Usamos `command -v` para resolverlos en el PATH.
CF := $(shell \
	for c in clang-format-20 clang-format-19 clang-format-18 clang-format-17 clang-format; do \
		if command -v $$c >/dev/null 2>&1; then \
			ver=$$($$c --version 2>&1 | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p'); \
			if [ "$$ver" -ge $(REQUIRED_MAJOR) ] 2>/dev/null; then echo "$$c"; break; fi; \
		fi; \
	done)

# Lista de todos los archivos C++ trackeados por git (.h, .cpp)
CPP_FILES := $(shell git ls-files '*.h' '*.cpp' 2>/dev/null)

.PHONY: help format check-format verify check-clang-format

help:  ## Muestra esta ayuda
	@echo 'ABDSharedCode — targets disponibles:'
	@echo ''
	@echo '  make format        Formatea todos los .h/.cpp con clang-format -i'
	@echo '  make check-format  Comprueba estilo sin modificar (dry-run -Werror)'
	@echo '  make verify        Ejecuta el hook pre-commit en modo VERIFY_ONLY'
	@echo '  make help          Muestra esta ayuda'
	@echo ''
	@echo 'Requiere clang-format $(REQUIRED_MAJOR)+ en el PATH.'

format:  ## Formatear todos los archivos .h/.cpp con clang-format -i
	@if [ -z "$(CF)" ]; then \
		echo "clang-format $(REQUIRED_MAJOR)+ no encontrado en el PATH."; \
		echo "Instalalo con: pip install clang-format==$(REQUIRED_MAJOR).*" ; \
		exit 1; \
	fi; \
	if [ -z "$(CPP_FILES)" ]; then \
		echo "No se encontraron archivos .h/.cpp en el repositorio."; \
		exit 0; \
	fi; \
	echo "Formateando con $(CF) ..."; \
	$(CF) -i $(CPP_FILES); \
	echo "Hecho. $(words $(CPP_FILES)) archivos formateados."; \
	echo "Haz commit de los cambios con: git add -A && git commit"

check-format:  ## Verificar estilo sin modificar archivos (clang-format --dry-run --Werror)
	@if [ -z "$(CF)" ]; then \
		echo "clang-format $(REQUIRED_MAJOR)+ no encontrado en el PATH."; \
		echo "Instalalo con: pip install clang-format==$(REQUIRED_MAJOR).*" ; \
		exit 1; \
	fi; \
	if [ -z "$(CPP_FILES)" ]; then \
		echo "No se encontraron archivos .h/.cpp en el repositorio."; \
		exit 0; \
	fi; \
	echo "Verificando estilo con $(CF) --dry-run --Werror ..."; \
	$(CF) --dry-run --Werror $(CPP_FILES); \
	echo "Verificacion completada: todos los archivos cumplen el estilo."

verify:  ## Ejecutar el hook pre-commit en modo VERIFY_ONLY (comprueba todos los archivos)
	@if [ ! -f .githooks/pre-commit ]; then \
		echo ".githooks/pre-commit no esta presente. Instala los hooks con:"; \
		echo "  bash .githooks/install.sh"; \
		exit 1; \
	fi; \
	VERIFY_ONLY=1 bash .githooks/pre-commit
