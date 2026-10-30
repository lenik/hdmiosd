# bash completion for hdmiosd

_hdmiosd()
{
	local cur prev words cword
	_init_completion || return

	case $prev in
	-d|--device)
		COMPREPLY=($(compgen -W "$(ls /dev/i2c-* /dev/video* 2>/dev/null)
			$(ls /sys/class/drm/card*-* 2>/dev/null | xargs -n1 basename 2>/dev/null)" -- "$cur"))
		# also allow path completion
		_filedir
		return
		;;
	-o|--opacity)
		COMPREPLY=($(compgen -W '50 75 90 95 100' -- "$cur"))
		return
		;;
	-g|--geom)
		COMPREPLY=($(compgen -W '400 520 520x420 640x480' -- "$cur"))
		return
		;;
	-l|--left|-r|--right|-t|--top|-b|--bottom)
		COMPREPLY=($(compgen -W '0 8 16 24 48' -- "$cur"))
		return
		;;
	esac

	if [[ $cur == -* ]]; then
		COMPREPLY=($(compgen -W '--device --opacity --geom --left --right --top --bottom --list --verbose --quiet --help --version -d -o -g -l -r -t -b -L -v -q -h' -- "$cur"))
		return
	fi
}

complete -F _hdmiosd hdmiosd
