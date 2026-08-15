from setuptools import setup, find_packages

with open("README.md", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="rtos-scheduler-viz",
    version="1.0.0",
    description="RTOS Scheduler Visualizer — decode FreeRTOS trace streams and render interactive Gantt charts",
    long_description=long_description,
    long_description_content_type="text/markdown",
    package_dir={"": "host"},
    packages=find_packages(where="host", exclude=["tests*"]),
    python_requires=">=3.11",
    install_requires=["rich>=13.0"],
    extras_require={
        "serial": ["pyserial>=3.5"],
        "dev":    ["pytest>=7.0", "pytest-cov>=4.0"],
    },
    entry_points={"console_scripts": ["rtos-viz=scheduler_viz.cli:main"]},
    classifiers=[
        "Programming Language :: Python :: 3",
        "Topic :: Software Development :: Embedded Systems",
        "Topic :: Software Development :: Debuggers",
    ],
)
