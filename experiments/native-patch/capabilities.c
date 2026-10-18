/* Reconstructed from the observed 150-105c capability builder, not vendor source.
 * Bound to existing private JSON functions by capabilities.ld.
 * Experimental replacement: the hosting_supported field changes to true.
 */
typedef void *Json;
extern Json json_object(void);
extern Json json_null(void);
extern Json json_bool(int);
extern Json json_string(const char *);
extern void json_add(Json, const char *, Json);
extern const char *machine_hostname(void);

__attribute__((section(".text.rebuilt_capabilities"), used))
Json rebuilt_capabilities(void) {
    Json capabilities = json_object();
    json_add(capabilities, "hosting_supported", json_bool(1));
    Json renderers = json_object();
    json_add(renderers, "0", json_string("opengl"));
    json_add(renderers, "5", json_string("vulkan"));
    json_add(capabilities, "renderers", renderers);
    json_add(capabilities, "hostname", json_string(machine_hostname()));
    Json envelope = json_object();
    json_add(envelope, "data", capabilities ? capabilities : json_null());
    return envelope;
}
