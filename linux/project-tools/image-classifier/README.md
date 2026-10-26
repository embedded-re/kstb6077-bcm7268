# CPU image-classification starter

Installed on the box at `/home/kstb/projects/image-classifier` with a shared
project virtual environment at `/home/kstb/projects/.venv`. It uses the system's
native Alpine NumPy, Pillow and ONNX Runtime packages. V3D compute is not
involved. The model is pretrained MobileNetV2 FP32, 1000 ImageNet classes.

As user `kstb`:

```sh
. ~/projects/.venv/bin/activate
cd ~/projects/image-classifier
python classify.py
python classify.py /path/to/your-picture.jpg --threads 2
```

The default sample should identify a Samoyed. This is image classification:
it assigns labels to the main image content, rather than drawing object
bounding boxes. It does not train a model. Small model probabilities are not
a measure of certainty about arbitrary images.

`--threads 2` leaves more CPU capacity for other apps; the measured fastest
setting for this model is four threads. Repeated warm model-only timings on
this box were about 245/146/103ms with one/two/four threads. They exclude disk
image loading, preprocessing, model startup, camera capture and display.
A separate full still-image pipeline test measured about 262ms per sample
including JPEG decode/preprocessing, about 3.8 images/second; this excludes
process/session startup.

## Recreate

On Alpine aarch64, install the native packages as administrator:

```sh
apk add py3-numpy py3-pillow py3-onnxruntime py3-pip
```

Then as `kstb`:

```sh
mkdir -p ~/projects/image-classifier
python3 -m venv --without-pip --system-site-packages ~/projects/.venv
```

Copy the scripts and root `assets.json` from this directory into the project.
Run `~/projects/.venv/bin/python fetch-assets.py` in that project to download
and SHA256-check the model, labels, sample image and model license. Asset URLs
and exact digests are recorded; an upstream replacement fails the check.
The initial model digest also matched the ONNX repository's Git LFS pointer.

The venv inherits the native APK libraries. Use `python -m pip` from inside
this venv for project-only additions; binary packages still need to support
Alpine/musl, aarch64 and the current Python version. This environment does not
provide PyTorch, TensorFlow, CUDA or GPU ONNX execution.

Model source/preprocessing: [official ONNX MobileNet documentation](https://github.com/onnx/models/tree/main/validated/vision/classification/mobilenet).
The model repository provides Apache 2.0 licensing, retained with the assets.
The demonstration image is fetched from [PyTorch Hub](https://github.com/pytorch/hub/blob/master/images/dog.jpg);
it is not embedded in this source repository. Use your own images for projects.

## Verification and bounded reliability check

```sh
python classify.py --self-test --benchmark 20 --report benchmark.json
python classify.py --self-test --threads 4 --soak-seconds 300 --report soak.json
```

Self-test requires the hashed bundled image and checks the expected Samoyed
class. Benchmark compares logits between CPU thread settings, and checks
finite/repeatable output. The soak holds and repeatedly verifies a 256MiB
integer memory pattern while classifying; it records temperature and available
RAM every five seconds. It stops at 80°C or below 256MiB available memory. These
are conservative test guards, not measured physical hardware thresholds.
Run heavy tests while the box is otherwise idle. Existing system monitoring
and an independent watchdog were used for the recorded preparation gate.

ONNX Runtime reports an unknown Broadcom CPU vendor and a failed GPU-vendor
file discovery on this custom display device. Explicit CPU execution passed;
these messages do not establish GPU support. The existing kernel scheduler
warnings are a separate known issue.

To archive projects onto the development PC, run
`python3 linux/project-tools/backup-workspace` from the repository on the PC.
It copies `/home/kstb/projects`, excluding the reproducible `.venv`, into
ignored `linux/build/project-workspaces/` and writes a SHA256 sidecar. Keep
large datasets and training work on the PC and copy only required inputs to
the box. No external disk or network mount was configured by this setup.
