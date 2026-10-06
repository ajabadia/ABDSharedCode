#pragma once

#include "ABDBankManagerCore.h"

namespace ABD::BankManager
{

/** Resultado de cargar contenido de producto. Los errores no detienen la carga
    de las entradas válidas, pero quedan expuestos para diagnóstico. */
struct FactoryContentLoadResult
{
    Library library;
    juce::StringArray errors;
    int discovered = 0;
    int loaded     = 0;

    bool succeeded() const noexcept { return loaded > 0 && errors.isEmpty(); }
};

/**
 * Parser y loader de contenido de fábrica.
 *
 * El formato de cada entrada es un .abdbank v2: ZIP con manifest.json,
 * blobs patch_*.bin y una imagen opcional. La función de recursos embebidos
 * se activa en los consumidores que definan ABD_FACTORY_CONTENT_EMBEDDED y
 * enlacen el target generado ABDFactoryContent.
 */
class FactoryContentLoader final
{
public:
    static constexpr int supportedManifestVersion = 2;

    static juce::Result loadBankFromZip(const void* data,
                                        size_t dataSize,
                                        Bank& destination);

    static FactoryContentLoadResult loadEmbedded();
};

} // namespace ABD::BankManager
