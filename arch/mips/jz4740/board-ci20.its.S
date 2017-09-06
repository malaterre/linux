/ {
	images {
		fdt@ci20 {
			description = "img,ci20 Device Tree";
			data = /incbin/("boot/dts/ingenic/ci20.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		conf@ci20 {
			description = "CI20 Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@ci20";
		};
	};
};
