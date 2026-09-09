"""
ABDSharedCode — CertificationAuditor
Motor de auditoria y validacion criptografica y acustica para paquetes de certificacion (.tar.gz).
Reutilizable en herramientas CLI, pipelines CI/CD y backend de Cloud API.
"""

import os
import io
import json
import zlib
import hashlib
import tarfile
from typing import Dict, Any, List, Optional

class CertificationAuditor:
    def __init__(self, schema_path: Optional[str] = None):
        if schema_path is None:
            schema_path = os.path.join(os.path.dirname(__file__), "certification_manifest.schema.json")
        self.schema_path = schema_path
        self._schema = None
        if os.path.exists(self.schema_path):
            with open(self.schema_path, "r", encoding="utf-8") as f:
                self._schema = json.load(f)

    def audit_package_bytes(self, package_bytes: bytes) -> Dict[str, Any]:
        """Audita un bundle .tar.gz recibido en memoria (bytes)."""
        tar_stream = io.BytesIO(package_bytes)
        try:
            with tarfile.open(fileobj=tar_stream, mode="r:gz") as tar:
                members = tar.getmembers()
                files = {}
                for m in members:
                    if m.isfile():
                        f = tar.extractfile(m)
                        if f:
                            files[os.path.basename(m.name)] = f.read()
                return self._audit_files(files, hashlib.sha256(package_bytes).hexdigest())
        except Exception as e:
            return {
                "status": "REJECTED",
                "errors": [f"El archivo no es un paquete .tar.gz valido: {str(e)}"],
                "warnings": [],
                "tier": "INVALID"
            }

    def audit_package_file(self, file_path: str) -> Dict[str, Any]:
        """Audita un bundle .tar.gz guardado en disco."""
        if not os.path.exists(file_path):
            return {
                "status": "REJECTED",
                "errors": [f"El archivo no existe: {file_path}"],
                "warnings": [],
                "tier": "INVALID"
            }
        with open(file_path, "rb") as f:
            data = f.read()
        return self.audit_package_bytes(data)

    def _audit_files(self, files: Dict[str, bytes], bundle_sha256: str) -> Dict[str, Any]:
        errors: List[str] = []
        warnings: List[str] = []

        manifest_file = None
        header_file = None
        html_file = None
        nam_file = None

        for name, data in files.items():
            if name.endswith("_manifest.json") or name == "manifest.json":
                manifest_file = (name, data)
            elif name.endswith("_lut.h") or name.endswith("_LUT.h"):
                header_file = (name, data)
            elif name.endswith("_certification_report.html") or name.endswith(".html"):
                html_file = (name, data)
            elif name.endswith(".nam"):
                nam_file = (name, data)

        # 1. Comprobar presencia de la triada minima
        if not manifest_file:
            errors.append("Falta el artefacto obligatorio: Manifiesto JSON (*_manifest.json)")
        if not header_file:
            errors.append("Falta el artefacto obligatorio: Cabecera C++ (*_lut.h)")
        if not html_file:
            errors.append("Falta el artefacto obligatorio: Reporte interactivo (*_certification_report.html)")

        if errors:
            return {
                "status": "REJECTED",
                "errors": errors,
                "warnings": warnings,
                "tier": "INVALID"
            }

        # 2. Parsear Manifiesto JSON
        try:
            manifest = json.loads(manifest_file[1].decode("utf-8"))
        except Exception as e:
            return {
                "status": "REJECTED",
                "errors": [f"Error al decodificar JSON del manifiesto: {str(e)}"],
                "warnings": warnings,
                "tier": "INVALID"
            }

        # 3. Validacion de campos obligatorios
        hw = manifest.get("hardware", {})
        hw_id = manifest.get("hardware_id") or hw.get("id")
        hw_name = hw.get("name") or manifest.get("displayName") or hw_id

        if not hw_id:
            errors.append("El manifiesto no declara 'hardware_id' o 'hardware.id'")

        sample_rate = manifest.get("sample_rate") or manifest.get("audio_calibration", {}).get("sample_rate") or manifest.get("audioCalibration", {}).get("sampleRate")
        if not sample_rate or sample_rate < 22050 or sample_rate > 192000:
            errors.append(f"Frecuencia de muestreo invalida o no declarada: {sample_rate}")

        # 4. Validar contenido de la cabecera C++
        header_text = header_file[1].decode("utf-8", errors="replace")
        if "alignas(16)" not in header_text and "alignas (16)" not in header_text:
            warnings.append("La cabecera C++ no declara explicitamente 'alignas(16)' para optimizacion SIMD")
        if "AbdBatchedPoint" not in header_text:
            warnings.append("La cabecera C++ no utiliza la estructura canonica 'AbdBatchedPoint'")

        # 5. Comprobar puntos medidos y calcular calidad acustica
        points = manifest.get("measured_points") or manifest.get("measuredPoints") or []
        snr_values = []
        thd_values = []

        for p in points:
            metrics = p.get("metrics", {})
            snr = metrics.get("snr_db") or metrics.get("snr")
            thd = metrics.get("thd_percent") or metrics.get("thd")
            if snr is not None:
                snr_values.append(float(snr))
            if thd is not None:
                thd_values.append(float(thd))

        avg_snr = sum(snr_values) / len(snr_values) if snr_values else 0.0
        avg_thd = sum(thd_values) / len(thd_values) if thd_values else 0.0

        if snr_values and avg_snr < 40.0:
            errors.append(f"Calidad acustica insuficiente: SNR medio de {avg_snr:.1f} dB es inferior al umbral minimo de 40 dB")

        # 6. Determinar Nivel de Certificacion (Tier)
        tier = "COMMUNITY"
        if not errors:
            if avg_snr >= 85.0 and len(points) >= 16:
                tier = "CERTIFIED_GOLD"
            elif avg_snr >= 65.0:
                tier = "CERTIFIED_SILVER"
            else:
                tier = "COMMUNITY"

        # 7. Checksum CRC32 si esta declarado
        declared_crc = manifest.get("crc32_checksum") or manifest.get("crc32Checksum")
        if declared_crc is not None:
            points_str = json.dumps(points, sort_keys=True)
            calc_crc = zlib.crc32(points_str.encode("utf-8")) & 0xFFFFFFFF
            if str(declared_crc) != str(calc_crc) and int(declared_crc) != calc_crc:
                warnings.append(f"El Checksum CRC-32 declarado ({declared_crc}) difiere del calculado ({calc_crc})")

        status = "APPROVED" if not errors else "REJECTED"

        return {
            "status": status,
            "tier": tier,
            "hardware_id": hw_id,
            "hardware_name": hw_name,
            "sample_rate": sample_rate,
            "measured_points_count": len(points),
            "average_snr_db": round(avg_snr, 1),
            "average_thd_percent": round(avg_thd, 3),
            "has_nam_model": (nam_file is not None),
            "bundle_sha256": bundle_sha256,
            "errors": errors,
            "warnings": warnings,
            "files_included": list(files.keys())
        }
