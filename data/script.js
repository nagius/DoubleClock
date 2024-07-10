document.addEventListener('DOMContentLoaded', () => {
    const apiUrl = '/settings';
    const alarmForm = document.getElementById('alarmForm');
    const alarmsContainer = document.getElementById('alarmsContainer');
    const saveButton = document.getElementById('saveButton');

    // Fetch and populate the form
    fetch(apiUrl)
        .then(response => response.json())
        .then(data => {
            populateForm(data.alarms);
        })
        .catch(error => console.error('Error fetching alarm data:', error));

    function populateForm(alarms) {
        alarms.forEach((alarm, index) => {
            const alarmDiv = document.createElement('div');
            alarmDiv.classList.add('alarm');

            alarmDiv.innerHTML = `
                <h3>Alarm ${index + 1}</h3>
                <div>
                    <label for="hour${index}">Hour:</label>
                    <input type="number" id="hour${index}" name="hour${index}" value="${alarm.hour}" min="0" max="23">
                </div>
                <div>
                    <label for="minute${index}">Minute:</label>
                    <input type="number" id="minute${index}" name="minute${index}" value="${alarm.minute}" min="0" max="59">
                </div>
                <div class="days">
                    ${['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'].map((day, dayIndex) => `
                        <label>
                            <input type="checkbox" name="day${index}" value="${dayIndex}" ${alarm.days[dayIndex] ? 'checked' : ''}>
                            ${day}
                        </label>
                    `).join('')}
                </div>
            `;

            alarmsContainer.appendChild(alarmDiv);
        });
    }

    saveButton.addEventListener('click', () => {
        const alarms = [];

        document.querySelectorAll('.alarm').forEach((alarmDiv, index) => {
            const hour = alarmDiv.querySelector(`input[name="hour${index}"]`).value;
            const minute = alarmDiv.querySelector(`input[name="minute${index}"]`).value;
            const days = Array.from(alarmDiv.querySelectorAll(`input[name="day${index}"]`)).map(input => input.checked);

            alarms.push({ hour: parseInt(hour), minute: parseInt(minute), days });
        });

        const payload = { alarms };

        fetch(apiUrl, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        })
        .then(response => response.json())
        .then(data => {
            console.log('Successfully saved alarm data:', data);
            alert('Settings saved successfully!');
        })
        .catch(error => console.error('Error saving alarm data:', error));
    });
});
