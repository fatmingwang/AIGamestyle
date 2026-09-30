if

<urlopen error [SSL: CERTIFICATE_VERIFY_FAILED]
certificate verify failed: unable to get local issuer
 certificate (_ssl.c:1006)>

happened

go to remote comfyUI server

gamestyleai\_comfyui\ComfyUI\.venv\Scripts\python.exe -c "import ssl; ssl._create_default_https_context = ssl._create_unverified_context; from transparent_background import Remover; r = Remover(jit=True); print('Done.')"


or make below contetn as a file

import ssl
ssl._create_default_https_context = ssl._create_unverified_context
from transparent_background import Remover
r = Remover(jit=True)
print('Done.')