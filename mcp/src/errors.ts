/**
 * Error types for the naina MCP server.
 *
 * Every error an agent can see must say what went wrong *and* what to do
 * about it -- a bare stack trace or a raw `naina::Error` message ("Engine::read:
 * model not found") is useless to a model that didn't write naina itself.
 */

/** Base for every error this server hands back as tool output. Message text
 * is already agent-facing; nothing upstream needs to reformat it. */
export abstract class NainaMcpError extends Error {}

/** The request itself was malformed: neither/both inputs given, bad base64,
 * an absurd size, a naina-rejected argument. Caller can fix and retry. */
export class InvalidInputError extends NainaMcpError {}

/** `path` does not resolve to a readable regular file. */
export class FileNotFoundError extends NainaMcpError {}

/** The bytes were readable but libvips (via sharp) could not decode them as
 * an image, or decoded to something naina can't take (e.g. zero pixels). */
export class UnsupportedImageError extends NainaMcpError {}

/** naina itself couldn't run: no backend compiled in, or model weights
 * absent and unreachable. Not fixable by changing the request. */
export class ModelUnavailableError extends NainaMcpError {}

/** naina ran but failed partway through (OOM, decode failure inside the
 * engine, etc). */
export class InferenceError extends NainaMcpError {}

/**
 * Turn an error thrown by `@jvoltci/naina`'s native Engine into one of the
 * above. The native binding throws plain `Error`s whose message is
 * `"<call site>: <naina_status_str>"` (see core/src/version.cc and
 * bindings/node/src/binding.cc) -- there is no error code on the JS side,
 * only that string, so we match on it.
 */
export function translateEngineError(err: unknown): NainaMcpError {
    if (err instanceof NainaMcpError) return err;

    const message = err instanceof Error ? err.message : String(err);

    if (/model not found/i.test(message)) {
        return new ModelUnavailableError(
            `Required model weights are not available (${message}). ` +
                'If NAINA_OFFLINE=1 is set in this server\'s environment, unset it so weights can ' +
                'be downloaded on first use, or pre-populate $NAINA_CACHE with them. This is not ' +
                'fixable by changing the request.',
        );
    }
    if (/backend unavail/i.test(message)) {
        return new ModelUnavailableError(
            `No inference backend is available in this naina build (${message}). ` +
                'The server needs to be built with a backend (e.g. ONNX Runtime) linked in, or ' +
                'NAINA_MCP_BACKEND needs to point at one that is actually installed.',
        );
    }
    if (/io error/i.test(message)) {
        return new ModelUnavailableError(
            `A model file could not be read or downloaded (${message}). Check network access and ` +
                'that $NAINA_CACHE is writable.',
        );
    }
    if (/out of memory/i.test(message)) {
        return new InferenceError(
            `Out of memory during inference (${message}). Retry with a smaller tier ("tiny" or ` +
                '"small") or a lower-resolution image.',
        );
    }
    if (/invalid argument/i.test(message)) {
        return new InvalidInputError(`naina rejected the decoded image (${message}).`);
    }
    if (/inference failed/i.test(message)) {
        return new InferenceError(`Inference failed on this image (${message}).`);
    }
    return new InferenceError(`naina failed to read the document (${message}).`);
}
