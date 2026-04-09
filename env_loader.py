import os

# Importar el módulo dotenv de PlatformIO (ya viene incluido en el ecosistema)
# Si no está disponible, usamos una implementación simple de lectura de archivo
Import("env")

def load_dotenv(path):
    if not os.path.exists(path):
        print(f"WARNING: .env file not found at {path}")
        return {}
    
    config = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, value = line.split("=", 1)
            # Limpiar comillas si existen
            value = value.strip().strip('"').strip("'")
            config[key.strip()] = value
    return config

# Cargar variables
dotenv_path = os.path.join(env.get("PROJECT_DIR"), ".env")
env_vars = load_dotenv(dotenv_path)

# Inyectar como build_flags
for key, value in env_vars.items():
    env.Append(BUILD_FLAGS=[f'-D{key}=\"{value}\"'])
    print(f"   [ENV] Inyectando variable: {key}")
